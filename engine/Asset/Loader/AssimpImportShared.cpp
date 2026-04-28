#include "AssimpImportShared.h"
#include "ImportSharedUtil.h"
#include "MaterialUtils.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRef.h>
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Mesh/MeshAsset.h>
#include <Asset/Scene/SceneAsset.h>
#include <Asset/Texture/TextureAsset.h>
#include <Core/Math/Transform.h>
#include <Framework/component/RenderComponent/LightComponent.h>
#include <Framework/component/RenderComponent/StaticMeshComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>

#include <SDL3/SDL.h>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/quaternion.hpp>

#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine::detail {
    namespace {
        /**
         * @brief Convert source-space vector to engine-space vector.
         *
         * FBX source basis: right-handed, Y-up, -Z forward.
         * Engine basis: right-handed, Z-up, Y forward.
         */
        glm::vec3 ConvertAxisToEngine(const glm::vec3 &value, const AssimpImportOptions &options) {
            if (!options.convert_coordinate_system) {
                return value;
            }
            return glm::vec3{value.x, -value.z, value.y};
        }

        /**
         * @brief Transform source-space position with import scale and axis conversion.
         */
        glm::vec3 TransformPosition(const aiVector3D &value, const AssimpImportOptions &options) {
            glm::vec3 transformed{value.x, value.y, value.z};
            transformed *= options.scale_factor;
            return ConvertAxisToEngine(transformed, options);
        }

        /**
         * @brief Transform source-space direction with optional axis conversion.
         */
        glm::vec3 TransformDirection(const aiVector3D &value, const AssimpImportOptions &options) {
            glm::vec3 transformed{value.x, value.y, value.z};
            transformed = ConvertAxisToEngine(transformed, options);
            if (glm::length(transformed) > 1e-5f) {
                transformed = glm::normalize(transformed);
            }
            return transformed;
        }

        struct MeshBuildOutput {
            MeshAsset *mesh_asset{nullptr};
            std::vector<uint32_t> submesh_material_indices{};
        };

        /**
         * @brief Read model file with Assimp and configured post-process flags.
         *
         * Throws std::runtime_error when Assimp returns null scene.
         */
        const aiScene *ReadAssimpScene(
            Assimp::Importer &importer, const std::filesystem::path &path, const AssimpImportOptions &options
        ) {
            // Configure Assimp post-process steps for stable static mesh import output.
            constexpr unsigned int k_postprocess_flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices
                                                         | aiProcess_GenSmoothNormals | aiProcess_ImproveCacheLocality
                                                         | aiProcess_FindInvalidData | aiProcess_ValidateDataStructure
                                                         | aiProcess_PreTransformVertices;

            const aiScene *scene = importer.ReadFile(path.string(), k_postprocess_flags);
            if (!scene) {
                throw std::runtime_error(importer.GetErrorString());
            }
            return scene;
        }

        /**
         * @brief Emit high-level diagnostics about scene capabilities and unsupported content.
         *
         * Current import path supports static mesh rendering data and a subset of lights.
         * Animations and skinning data are logged and ignored.
         */
        void LogSceneSummaryAndWarnings(const aiScene &scene) {
            SDL_LogInfo(
                SDL_LOG_CATEGORY_APPLICATION,
                "Assimp scene summary: meshes=%u materials=%u animations=%u cameras=%u lights=%u textures=%u",
                scene.mNumMeshes,
                scene.mNumMaterials,
                scene.mNumAnimations,
                scene.mNumCameras,
                scene.mNumLights,
                scene.mNumTextures
            );

            if (scene.mNumAnimations > 0) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Detected %u animation(s); animations are not supported yet and will be skipped.",
                    scene.mNumAnimations
                );
            }

            bool has_bones = false;
            for (unsigned int mesh_index = 0; mesh_index < scene.mNumMeshes; ++mesh_index) {
                if (scene.mMeshes[mesh_index] && scene.mMeshes[mesh_index]->mNumBones > 0) {
                    has_bones = true;
                    break;
                }
            }
            if (has_bones) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Detected skeletal data; skinning is not supported yet and will be skipped."
                );
            }

            if (scene.mNumMeshes == 0) {
                throw std::runtime_error("Imported scene does not contain any mesh.");
            }
        }

        /**
         * @brief Convert Assimp meshes into one engine MeshAsset containing submeshes.
         *
         * This stage builds packed vertex attribute buffers and index buffers,
         * fills defaults for missing optional channels (color/uv), and requires normals.
         * Also records per-submesh material index mapping for later material binding.
         */
        MeshBuildOutput BuildMeshAssetFromScene(
            const aiScene &scene, AssetManager &am, const std::string &model_name, const AssimpImportOptions &options
        ) {
            auto *mesh_asset = am.CreateAsset<MeshAsset>();
            mesh_asset->m_name = model_name;
            mesh_asset->m_submeshes.reserve(scene.mNumMeshes);

            std::vector<uint32_t> submesh_material_indices;
            submesh_material_indices.reserve(scene.mNumMeshes);

            // Convert Assimp mesh streams into engine submesh layout and vertex buffers.
            for (unsigned int mesh_index = 0; mesh_index < scene.mNumMeshes; ++mesh_index) {
                const aiMesh *ai_mesh = scene.mMeshes[mesh_index];
                if (!ai_mesh || ai_mesh->mNumVertices == 0 || ai_mesh->mNumFaces == 0) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Skipping empty mesh at index %u.", mesh_index);
                    continue;
                }

                submesh_material_indices.push_back(ai_mesh->mMaterialIndex);
                mesh_asset->m_submeshes.emplace_back();
                auto &submesh = mesh_asset->m_submeshes.back();
                submesh.vertex_count = ai_mesh->mNumVertices;
                const size_t vertex_count = static_cast<size_t>(ai_mesh->mNumVertices);

                const bool has_normals = ai_mesh->HasNormals();
                const bool has_colors = ai_mesh->HasVertexColors(0);
                const bool has_texcoord0 = ai_mesh->HasTextureCoords(0);
                if (!has_normals) {
                    throw std::runtime_error("Mesh does not have normals.");
                }

                std::vector<float> positions;
                positions.resize(vertex_count * 3);
                std::vector<float> normals;
                normals.resize(vertex_count * 3);
                std::vector<float> colors;
                if (has_colors) {
                    colors.resize(vertex_count * 3);
                }
                std::vector<float> texcoord0;
                if (has_texcoord0) {
                    texcoord0.resize(vertex_count * 2);
                }

                submesh.positions.type = VertexAttributeType::SFloat32x3;
                for (unsigned int vertex_index = 0; vertex_index < ai_mesh->mNumVertices; ++vertex_index) {
                    const size_t v = static_cast<size_t>(vertex_index);
                    const glm::vec3 position = TransformPosition(ai_mesh->mVertices[vertex_index], options);
                    positions[v * 3] = position.x;
                    positions[v * 3 + 1] = position.y;
                    positions[v * 3 + 2] = position.z;

                    const glm::vec3 normal = TransformDirection(ai_mesh->mNormals[vertex_index], options);
                    normals[v * 3] = normal.x;
                    normals[v * 3 + 1] = normal.y;
                    normals[v * 3 + 2] = normal.z;

                    if (has_colors) {
                        const aiColor4D &color = ai_mesh->mColors[0][vertex_index];
                        colors[v * 3] = color.r;
                        colors[v * 3 + 1] = color.g;
                        colors[v * 3 + 2] = color.b;
                    }

                    if (has_texcoord0) {
                        const aiVector3D &uv = ai_mesh->mTextureCoords[0][vertex_index];
                        texcoord0[v * 2] = uv.x;
                        texcoord0[v * 2 + 1] = uv.y;
                    }
                }

                submesh.m_indices.reserve(static_cast<size_t>(ai_mesh->mNumFaces) * 3);
                for (unsigned int face_index = 0; face_index < ai_mesh->mNumFaces; ++face_index) {
                    const aiFace &face = ai_mesh->mFaces[face_index];
                    if (face.mNumIndices != 3) {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "Skipping non-triangle face in mesh %u, face %u.",
                            mesh_index,
                            face_index
                        );
                        continue;
                    }
                    submesh.m_indices.push_back(face.mIndices[0]);
                    submesh.m_indices.push_back(face.mIndices[1]);
                    submesh.m_indices.push_back(face.mIndices[2]);
                }

                submesh.m_vertex_attributes.reserve(
                    positions.size() * sizeof(float) + normals.size() * sizeof(float) + colors.size() * sizeof(float)
                    + texcoord0.size() * sizeof(float)
                );

                import_shared::AppendVertexAttribute(
                    submesh.m_vertex_attributes, positions.data(), positions.size(), submesh.positions
                );
                if (!colors.empty()) {
                    submesh.color.type = VertexAttributeType::SFloat32x3;
                    import_shared::AppendVertexAttribute(
                        submesh.m_vertex_attributes, colors.data(), colors.size(), submesh.color
                    );
                } else {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Mesh %u does not have vertex colors; fill with default color.",
                        mesh_index
                    );
                    std::vector<float> default_colors(static_cast<size_t>(ai_mesh->mNumVertices) * 3, 1.0f);
                    submesh.color.type = VertexAttributeType::SFloat32x3;
                    import_shared::AppendVertexAttribute(
                        submesh.m_vertex_attributes, default_colors.data(), default_colors.size(), submesh.color
                    );
                }
                submesh.normal.type = VertexAttributeType::SFloat32x3;
                import_shared::AppendVertexAttribute(
                    submesh.m_vertex_attributes, normals.data(), normals.size(), submesh.normal
                );
                if (!texcoord0.empty()) {
                    submesh.texcoord0.type = VertexAttributeType::SFloat32x2;
                    import_shared::AppendVertexAttribute(
                        submesh.m_vertex_attributes, texcoord0.data(), texcoord0.size(), submesh.texcoord0
                    );
                } else {
                    std::vector<float> default_uvs(static_cast<size_t>(ai_mesh->mNumVertices) * 2, 0.0f);
                    submesh.texcoord0.type = VertexAttributeType::SFloat32x2;
                    import_shared::AppendVertexAttribute(
                        submesh.m_vertex_attributes, default_uvs.data(), default_uvs.size(), submesh.texcoord0
                    );
                }
            }

            if (mesh_asset->m_submeshes.empty()) {
                throw std::runtime_error("No valid static mesh could be extracted from scene.");
            }

            MeshBuildOutput output{};
            output.mesh_asset = mesh_asset;
            output.submesh_material_indices = std::move(submesh_material_indices);
            return output;
        }

        /**
         * @brief Convert supported Assimp lights into engine scene light components.
         *
         * Currently supports directional and point lights. Unsupported light types
         * are logged and skipped. Returns number of successfully imported lights.
         */
        uint32_t AppendLightsToScene(
            const aiScene &scene, Scene &temp_scene, const std::string &model_name, const AssimpImportOptions &options
        ) {
            uint32_t imported_light_count = 0;
            for (unsigned int light_index = 0; light_index < scene.mNumLights; ++light_index) {
                const aiLight *ai_light = scene.mLights[light_index];
                if (!ai_light) {
                    continue;
                }

                auto &light_go = temp_scene.CreateGameObject();
                light_go.m_name = model_name + "_light_" + std::to_string(light_index);
                auto &light_component = light_go.AddComponent<LightComponent>();
                light_component.m_color =
                    glm::vec3{ai_light->mColorDiffuse.r, ai_light->mColorDiffuse.g, ai_light->mColorDiffuse.b};
                light_component.m_intensity = 1.0f;
                light_component.m_cast_shadow = false;

                Transform transform{};
                if (ai_light->mType == aiLightSource_DIRECTIONAL) {
                    light_component.m_type = LightType::Directional;
                    glm::vec3 direction = TransformDirection(ai_light->mDirection, options);
                    if (glm::length(direction) > 1e-5f) {
                        transform.SetRotation(glm::rotation(glm::vec3{0.0f, 1.0f, 0.0f}, direction));
                    }
                    light_go.SetTransform(transform);
                    ++imported_light_count;
                } else if (ai_light->mType == aiLightSource_POINT) {
                    light_component.m_type = LightType::Point;
                    transform.SetPosition(TransformPosition(ai_light->mPosition, options));
                    light_go.SetTransform(transform);
                    ++imported_light_count;
                } else {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Light %s has unsupported type %d and will be skipped.",
                        ai_light->mName.C_Str(),
                        static_cast<int>(ai_light->mType)
                    );
                }
            }
            return imported_light_count;
        }
    } // namespace

    ImportResult ImportWithAssimp(
        const std::filesystem::path &path,
        const std::optional<std::filesystem::path> &path_in_project,
        const std::weak_ptr<AssetManager> &asset_manager,
        const std::weak_ptr<FileSystemDatabase> &database,
        const AssimpImportOptions &options
    ) {
        /**
         * Import pipeline overview:
         * 1) Validate mode and dependency handles.
         * 2) Load + validate Assimp scene.
         * 3) Build runtime mesh/material/texture assets.
         * 4) Optionally persist generated assets.
         * 5) Optionally build scene payload and return refs.
         */
        if (options.persist_assets && !path_in_project.has_value()) {
            throw std::invalid_argument("Persisted import requires a valid path_in_project.");
        }

        // 1) Resolve context objects and load source scene through Assimp.
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering %s loader: %s", options.source_name, path.string().c_str());
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "Import transform options: scale_factor=%.6f convert_coordinate_system=%s",
            options.scale_factor,
            options.convert_coordinate_system ? "true" : "false"
        );

        auto db = database.lock();
        assert(db);
        auto am = asset_manager.lock();
        assert(am);

        Assimp::Importer importer;
        const aiScene *scene = ReadAssimpScene(importer, path, options);
        LogSceneSummaryAndWarnings(*scene);

        std::unordered_map<std::string, uint32_t> name_counters;
        const std::string model_name = path.stem().string();
        name_counters.reserve(scene->mNumMaterials + scene->mNumMeshes + 16);

        // 2) Build mesh/material/texture assets from scene data.
        const MeshBuildOutput mesh_output = BuildMeshAssetFromScene(*scene, *am, model_name, options);
        MaterialBuildOutput material_output = BuildMaterialsFromAssimp(
            *scene, path, *am, *db, model_name, mesh_output.submesh_material_indices, name_counters
        );

        // 3) Assemble import result references used by runtime or caller-side tests.
        ImportResult result{};
        result.mesh_asset = AssetRef(mesh_output.mesh_asset->GetGUID());
        result.mesh_material_assets.reserve(mesh_output.submesh_material_indices.size());
        result.created_material_assets.reserve(material_output.created_material_assets.size());
        result.created_texture_assets.reserve(material_output.created_texture_assets.size());
        for (uint32_t material_index : mesh_output.submesh_material_indices) {
            auto it = material_output.material_refs.find(static_cast<size_t>(material_index));
            result.mesh_material_assets.push_back(
                it == material_output.material_refs.end() ? material_output.default_blinn_material : it->second
            );
        }
        for (const auto *material_asset : material_output.created_material_assets) {
            result.created_material_assets.emplace_back(material_asset->GetGUID());
        }
        for (const auto *texture_asset : material_output.created_texture_assets) {
            result.created_texture_assets.emplace_back(texture_asset->GetGUID());
        }

        // 4) Persist generated assets only when caller explicitly enables storage output.
        if (options.persist_assets) {
            const auto &target_path = *path_in_project;
            import_shared::SaveAsset(*db, *mesh_output.mesh_asset, target_path, mesh_output.mesh_asset->m_name);
            for (const auto *material_asset : material_output.created_material_assets) {
                import_shared::SaveAsset(*db, *material_asset, target_path, material_asset->m_name);
            }
            for (const auto *texture_asset : material_output.created_texture_assets) {
                import_shared::SaveAsset(*db, *texture_asset, target_path, texture_asset->m_name);
            }
        }

        // 5) Optionally construct scene asset payload (mesh + materials + imported lights).
        if (options.create_scene_asset) {
            auto &temp_scene = MainClass::GetInstance()->GetWorldSystem()->CreateScene();
            auto &go = temp_scene.CreateGameObject();
            go.m_name = mesh_output.mesh_asset->m_name;
            auto &mesh_component = go.AddComponent<StaticMeshComponent>();
            mesh_component.m_mesh_asset = result.mesh_asset;
            mesh_component.m_material_assets = result.mesh_material_assets;

            result.imported_light_count = AppendLightsToScene(*scene, temp_scene, model_name, options);

            temp_scene.FlushCmdQueue();

            auto *scene_asset = am->CreateAsset<SceneAsset>();
            scene_asset->SaveFromScene(temp_scene);
            if (options.persist_assets) {
                import_shared::SaveAsset(*db, *scene_asset, *path_in_project, "GO_" + mesh_output.mesh_asset->m_name);
            }
            result.scene_asset = AssetRef(scene_asset->GetGUID());
        }

        return result;
    }

    /**
     * @brief Compatibility wrapper for legacy persisted import API.
     *
     * Forces persistence and scene-asset generation behavior regardless of caller flags,
     * then delegates actual work to ImportWithAssimp.
     */
    void LoadResourceWithAssimp(
        const std::filesystem::path &path,
        const std::filesystem::path &path_in_project,
        const std::weak_ptr<AssetManager> &asset_manager,
        const std::weak_ptr<FileSystemDatabase> &database,
        const AssimpImportOptions &options
    ) {
        AssimpImportOptions runtime_options = options;
        runtime_options.persist_assets = true;
        runtime_options.create_scene_asset = true;
        static_cast<void>(ImportWithAssimp(path, path_in_project, asset_manager, database, runtime_options));
    }
} // namespace Engine::detail
