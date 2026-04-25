#include "AssimpImportShared.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRef.h>
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Mesh/MeshAsset.h>
#include <Asset/Scene/SceneAsset.h>
#include <Asset/Texture/Image2DTextureAsset.h>
#include <Asset/Texture/SolidColorTextureAsset.h>
#include <Core/Math/Transform.h>
#include <Framework/component/RenderComponent/LightComponent.h>
#include <Framework/component/RenderComponent/StaticMeshComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Reflection/Archive.h>

#include <SDL3/SDL.h>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <gtx/quaternion.hpp>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    std::string SanitizeAssetName(std::string name) {
        if (name.empty()) {
            return "unnamed";
        }
        std::replace_if(
            name.begin(),
            name.end(),
            [](unsigned char ch) {
                switch (ch) {
                case '<':
                case '>':
                case ':':
                case '"':
                case '/':
                case '\\':
                case '|':
                case '?':
                case '*':
                    return true;
                default:
                    return std::iscntrl(ch) != 0;
                }
            },
            '_'
        );
        return name;
    }

    std::string MakeUniqueAssetName(
        const std::string &base_name, std::unordered_map<std::string, uint32_t> &name_counters
    ) {
        std::string sanitized = SanitizeAssetName(base_name);
        auto [it, inserted] = name_counters.try_emplace(sanitized, 0);
        if (inserted) {
            return sanitized;
        }
        it->second += 1;
        return sanitized + "_" + std::to_string(it->second);
    }

    glm::vec4 ToGlm(const aiColor4D &color) {
        return glm::vec4{color.r, color.g, color.b, color.a};
    }

    std::optional<std::filesystem::path> ResolveTexturePath(
        const std::filesystem::path &model_path, const aiString &texture_path
    ) {
        const std::string raw_texture_path = texture_path.C_Str();
        if (raw_texture_path.empty() || raw_texture_path[0] == '*') {
            return std::nullopt;
        }

        const std::filesystem::path candidate_path(raw_texture_path);
        const std::vector<std::filesystem::path> candidates = {
            candidate_path,
            model_path.parent_path() / candidate_path,
            model_path.parent_path() / candidate_path.filename()
        };
        for (const auto &candidate : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec)) {
                return std::filesystem::weakly_canonical(candidate, ec);
            }
        }
        return std::nullopt;
    }

    void AppendVertexAttribute(
        std::vector<std::byte> &buffer,
        const float *data,
        size_t float_count,
        Engine::MeshAsset::Submesh::Attributes &attr
    ) {
        attr.buffer_offset = buffer.size();
        const auto *begin = reinterpret_cast<const std::byte *>(data);
        const auto *end = reinterpret_cast<const std::byte *>(data + float_count);
        buffer.insert(buffer.end(), begin, end);
        attr.buffer_size = float_count * sizeof(float);
    }
} // namespace

namespace Engine::detail {
    namespace {
        AssetPath MakeAssetPath(
            FileSystemDatabase &database, const std::filesystem::path &path_in_project, const std::string &asset_name
        ) {
            return AssetPath(database, path_in_project / (asset_name + ".asset"));
        }

        void SaveAsset(
            FileSystemDatabase &database,
            const Asset &asset,
            const std::filesystem::path &path_in_project,
            const std::string &asset_name
        ) {
            Serialization::Archive archive;
            archive.prepare_save();
            asset.save_asset_to_archive(archive);
            database.SaveArchive(archive, MakeAssetPath(database, path_in_project, asset_name));
        }

        bool HasTextureType(const aiMaterial &material, aiTextureType texture_type) {
            return material.GetTextureCount(texture_type) > 0;
        }

        bool ShouldUsePBR(const aiMaterial &material) {
            float metallic_factor = 0.0f;
            float roughness_factor = 0.0f;

            if (material.Get(AI_MATKEY_METALLIC_FACTOR, metallic_factor) == aiReturn_SUCCESS) {
                return true;
            }
            if (material.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness_factor) == aiReturn_SUCCESS) {
                return true;
            }

            return HasTextureType(material, aiTextureType_BASE_COLOR)
                   || HasTextureType(material, aiTextureType_METALNESS)
                   || HasTextureType(material, aiTextureType_DIFFUSE_ROUGHNESS)
                   || HasTextureType(material, aiTextureType_AMBIENT_OCCLUSION);
        }
    } // namespace

    void LoadResourceWithAssimp(
        const std::filesystem::path &path,
        const std::filesystem::path &path_in_project,
        const std::weak_ptr<AssetManager> &asset_manager,
        const std::weak_ptr<FileSystemDatabase> &database,
        const AssimpImportOptions &options
    ) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering %s loader: %s", options.source_name, path.string().c_str());

        Assimp::Importer importer;
        if (options.enable_fbx_compat) {
            importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
        }

        constexpr unsigned int k_postprocess_flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices
                                                     | aiProcess_GenSmoothNormals | aiProcess_ImproveCacheLocality
                                                     | aiProcess_FindInvalidData | aiProcess_ValidateDataStructure
                                                     | aiProcess_PreTransformVertices;

        const aiScene *scene = importer.ReadFile(path.string(), k_postprocess_flags);
        if (!scene) {
            throw std::runtime_error(importer.GetErrorString());
        }

        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "Assimp scene summary: meshes=%u materials=%u animations=%u cameras=%u lights=%u textures=%u",
            scene->mNumMeshes,
            scene->mNumMaterials,
            scene->mNumAnimations,
            scene->mNumCameras,
            scene->mNumLights,
            scene->mNumTextures
        );

        if (scene->mNumAnimations > 0) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Detected %u animation(s); animations are not supported yet and will be skipped.",
                scene->mNumAnimations
            );
        }

        bool has_bones = false;
        for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
            if (scene->mMeshes[mesh_index] && scene->mMeshes[mesh_index]->mNumBones > 0) {
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

        if (scene->mNumMeshes == 0) {
            throw std::runtime_error("Imported scene does not contain any mesh.");
        }

        auto db = database.lock();
        assert(db);
        auto am = asset_manager.lock();
        assert(am);

        std::unordered_map<std::string, uint32_t> name_counters;
        const std::string model_name = path.stem().string();

        auto mesh_asset = am->CreateAsset<MeshAsset>();
        mesh_asset->m_name = model_name;

        std::vector<uint32_t> submesh_material_indices;
        submesh_material_indices.reserve(scene->mNumMeshes);

        for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
            const aiMesh *ai_mesh = scene->mMeshes[mesh_index];
            if (!ai_mesh || ai_mesh->mNumVertices == 0 || ai_mesh->mNumFaces == 0) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Skipping empty mesh at index %u.", mesh_index);
                continue;
            }

            submesh_material_indices.push_back(ai_mesh->mMaterialIndex);
            mesh_asset->m_submeshes.emplace_back();
            auto &submesh = mesh_asset->m_submeshes.back();
            submesh.vertex_count = ai_mesh->mNumVertices;

            std::vector<float> positions;
            positions.reserve(static_cast<size_t>(ai_mesh->mNumVertices) * 3);
            std::vector<float> normals;
            normals.reserve(static_cast<size_t>(ai_mesh->mNumVertices) * 3);
            std::vector<float> colors;
            colors.reserve(static_cast<size_t>(ai_mesh->mNumVertices) * 3);
            std::vector<float> texcoord0;
            texcoord0.reserve(static_cast<size_t>(ai_mesh->mNumVertices) * 2);

            submesh.positions.type = VertexAttributeType::SFloat32x3;
            for (unsigned int vertex_index = 0; vertex_index < ai_mesh->mNumVertices; ++vertex_index) {
                const aiVector3D &position = ai_mesh->mVertices[vertex_index];
                positions.push_back(position.x);
                positions.push_back(position.y);
                positions.push_back(position.z);

                if (ai_mesh->HasNormals()) {
                    const aiVector3D &normal = ai_mesh->mNormals[vertex_index];
                    normals.push_back(normal.x);
                    normals.push_back(normal.y);
                    normals.push_back(normal.z);
                }

                if (ai_mesh->HasVertexColors(0)) {
                    const aiColor4D &color = ai_mesh->mColors[0][vertex_index];
                    colors.push_back(color.r);
                    colors.push_back(color.g);
                    colors.push_back(color.b);
                }

                if (ai_mesh->HasTextureCoords(0)) {
                    const aiVector3D &uv = ai_mesh->mTextureCoords[0][vertex_index];
                    texcoord0.push_back(uv.x);
                    texcoord0.push_back(uv.y);
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

            AppendVertexAttribute(submesh.m_vertex_attributes, positions.data(), positions.size(), submesh.positions);
            if (!colors.empty()) {
                submesh.color.type = VertexAttributeType::SFloat32x3;
                AppendVertexAttribute(submesh.m_vertex_attributes, colors.data(), colors.size(), submesh.color);
            } else {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Mesh %u does not have vertex colors; fill with default color.",
                    mesh_index
                );
                std::vector<float> default_colors(static_cast<size_t>(ai_mesh->mNumVertices) * 3, 1.0f);
                submesh.color.type = VertexAttributeType::SFloat32x3;
                AppendVertexAttribute(
                    submesh.m_vertex_attributes, default_colors.data(), default_colors.size(), submesh.color
                );
            }
            if (!normals.empty()) {
                submesh.normal.type = VertexAttributeType::SFloat32x3;
                AppendVertexAttribute(submesh.m_vertex_attributes, normals.data(), normals.size(), submesh.normal);
            } else {
                throw std::runtime_error("Mesh does not have normals.");
            }
            if (!texcoord0.empty()) {
                submesh.texcoord0.type = VertexAttributeType::SFloat32x2;
                AppendVertexAttribute(
                    submesh.m_vertex_attributes, texcoord0.data(), texcoord0.size(), submesh.texcoord0
                );
            } else {
                std::vector<float> default_uvs(static_cast<size_t>(ai_mesh->mNumVertices) * 2, 0.0f);
                submesh.texcoord0.type = VertexAttributeType::SFloat32x2;
                AppendVertexAttribute(
                    submesh.m_vertex_attributes, default_uvs.data(), default_uvs.size(), submesh.texcoord0
                );
            }
        }

        if (mesh_asset->m_submeshes.empty()) {
            throw std::runtime_error("No valid static mesh could be extracted from scene.");
        }

        std::unordered_map<uint32_t, MaterialAsset *> material_assets;
        std::unordered_map<uint32_t, AssetRef> material_refs;
        std::vector<TextureAsset *> texture_assets;
        std::unordered_map<std::string, AssetRef> texture_refs_by_path;
        const AssetRef default_blinn_material = db->GetNewAssetRef(
            AssetPath(*db, std::filesystem::path("~/materials/solid_color_dark_grey_blinn_phong.asset"))
        );
        const AssetRef default_pbr_albedo =
            db->GetNewAssetRef(AssetPath(*db, std::filesystem::path("~/textures/dark_grey.asset")));
        const AssetRef default_pbr_mrao =
            db->GetNewAssetRef(AssetPath(*db, std::filesystem::path("~/textures/white.asset")));

        for (uint32_t material_index : submesh_material_indices) {
            if (material_refs.contains(material_index)) {
                continue;
            }
            if (material_index >= scene->mNumMaterials || scene->mMaterials[material_index] == nullptr) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Mesh references invalid material slot %u; falling back to builtin default material.",
                    material_index
                );
                material_refs[material_index] = default_blinn_material;
                continue;
            }

            const aiMaterial &source_material = *scene->mMaterials[material_index];
            auto material_asset = am->CreateAsset<MaterialAsset>();

            aiString source_name;
            source_material.Get(AI_MATKEY_NAME, source_name);
            const std::string fallback_material_name = model_name + "_material_" + std::to_string(material_index);
            const std::string base_material_name =
                source_name.length > 0 ? source_name.C_Str() : fallback_material_name;
            material_asset->m_name = MakeUniqueAssetName(base_material_name, name_counters);

            auto create_solid_color_texture = [&](const aiColor4D &color, const char *suffix) -> AssetRef {
                auto solid_color = am->CreateAsset<SolidColorTextureAsset>();
                solid_color->m_name = MakeUniqueAssetName(material_asset->m_name + suffix, name_counters);
                solid_color->m_color = ToGlm(color);
                texture_assets.push_back(solid_color);
                return AssetRef(solid_color->GetGUID());
            };

            auto try_load_texture_ref = [&](aiTextureType texture_type, MaterialProperty &out_prop) -> bool {
                if (source_material.GetTextureCount(texture_type) == 0) {
                    return false;
                }
                aiString texture_path;
                aiTextureMapping mapping = aiTextureMapping_UV;
                unsigned int uv_index = 0;
                if (source_material.GetTexture(texture_type, 0, &texture_path, &mapping, &uv_index)
                    != aiReturn_SUCCESS) {
                    return false;
                }
                if (texture_path.length > 0 && texture_path.C_Str()[0] == '*') {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Material %s uses embedded texture %s; embedded textures are not supported and will be "
                        "skipped.",
                        material_asset->m_name.c_str(),
                        texture_path.C_Str()
                    );
                    return false;
                }
                if (uv_index != 0) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Material %s uses UV set %u; only UV0 is supported, texture will be skipped.",
                        material_asset->m_name.c_str(),
                        uv_index
                    );
                    return false;
                }

                auto resolved_texture_path = ResolveTexturePath(path, texture_path);
                if (!resolved_texture_path) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Missing external texture %s referenced by material %s.",
                        texture_path.C_Str(),
                        material_asset->m_name.c_str()
                    );
                    return false;
                }

                const std::string resolved_key = resolved_texture_path->generic_string();
                auto cached = texture_refs_by_path.find(resolved_key);
                if (cached != texture_refs_by_path.end()) {
                    out_prop = MaterialProperty(cached->second, MaterialProperty::Type::Texture);
                    return true;
                }

                auto image_texture = am->CreateAsset<Image2DTextureAsset>();
                image_texture->LoadFromFile(*resolved_texture_path);
                image_texture->m_name =
                    MakeUniqueAssetName(model_name + "_" + resolved_texture_path->stem().string(), name_counters);
                texture_assets.push_back(image_texture);

                const AssetRef texture_ref(image_texture);
                texture_refs_by_path.emplace(resolved_key, texture_ref);
                out_prop = MaterialProperty(texture_ref, MaterialProperty::Type::Texture);
                return true;
            };

            const bool use_pbr = ShouldUsePBR(source_material);
            if (use_pbr) {
                material_asset->m_library = db->GetNewAssetRef(
                    AssetPath(*db, std::filesystem::path("~/material_libraries/PBRLibrary.asset"))
                );

                aiColor4D base_color(0.2f, 0.2f, 0.2f, 1.0f);
                source_material.Get(AI_MATKEY_BASE_COLOR, base_color);

                MaterialProperty albedo_prop;
                if (!try_load_texture_ref(aiTextureType_BASE_COLOR, albedo_prop)
                    && !try_load_texture_ref(aiTextureType_DIFFUSE, albedo_prop)) {
                    if (source_material.GetTextureCount(aiTextureType_BASE_COLOR) == 0
                        && source_material.GetTextureCount(aiTextureType_DIFFUSE) == 0) {
                        const AssetRef fallback_ref =
                            (base_color.r == 0.2f && base_color.g == 0.2f && base_color.b == 0.2f)
                                ? default_pbr_albedo
                                : create_solid_color_texture(base_color, "_albedo");
                        albedo_prop = MaterialProperty(fallback_ref, MaterialProperty::Type::Texture);
                    }
                }
                material_asset->m_properties["albedoSampler"] =
                    albedo_prop.m_type == MaterialProperty::Type::Texture
                        ? albedo_prop
                        : MaterialProperty(default_pbr_albedo, MaterialProperty::Type::Texture);

                MaterialProperty mrao_prop;
                const bool has_mrao = try_load_texture_ref(aiTextureType_UNKNOWN, mrao_prop)
                                      || try_load_texture_ref(aiTextureType_METALNESS, mrao_prop)
                                      || try_load_texture_ref(aiTextureType_DIFFUSE_ROUGHNESS, mrao_prop)
                                      || try_load_texture_ref(aiTextureType_AMBIENT_OCCLUSION, mrao_prop);
                material_asset->m_properties["MRAOSampler"] =
                    has_mrao ? mrao_prop : MaterialProperty(default_pbr_mrao, MaterialProperty::Type::Texture);

                float metallic_factor = 1.0f;
                float roughness_factor = 1.0f;
                source_material.Get(AI_MATKEY_METALLIC_FACTOR, metallic_factor);
                source_material.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness_factor);

                material_asset->m_properties["metalness_scale"] = metallic_factor;
                material_asset->m_properties["roughness_scale"] = roughness_factor;

                if (source_material.GetTextureCount(aiTextureType_NORMALS) > 0
                    || source_material.GetTextureCount(aiTextureType_EMISSIVE) > 0
                    || source_material.GetTextureCount(aiTextureType_OPACITY) > 0
                    || source_material.GetTextureCount(aiTextureType_HEIGHT) > 0) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Material %s contains textures beyond current PBR support (normal/emissive/opacity/height).",
                        material_asset->m_name.c_str()
                    );
                }
            } else {
                material_asset->m_library = db->GetNewAssetRef(
                    AssetPath(*db, std::filesystem::path("~/material_libraries/BlinnPhongLibrary.asset"))
                );

                aiColor4D diffuse_color(1.0f, 1.0f, 1.0f, 1.0f);
                if (source_material.Get(AI_MATKEY_BASE_COLOR, diffuse_color) != aiReturn_SUCCESS) {
                    source_material.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse_color);
                }
                aiColor4D specular_color(0.5f, 0.5f, 0.5f, 1.0f);
                source_material.Get(AI_MATKEY_COLOR_SPECULAR, specular_color);
                float shininess = 32.0f;
                source_material.Get(AI_MATKEY_SHININESS, shininess);

                material_asset->m_properties["ambient_color"] =
                    glm::vec4{diffuse_color.r, diffuse_color.g, diffuse_color.b, 1.0f};
                material_asset->m_properties["specular_color"] =
                    glm::vec4{specular_color.r, specular_color.g, specular_color.b, shininess};

                MaterialProperty base_tex_prop;
                if (!try_load_texture_ref(aiTextureType_BASE_COLOR, base_tex_prop)
                    && !try_load_texture_ref(aiTextureType_DIFFUSE, base_tex_prop)) {
                    base_tex_prop = MaterialProperty(
                        create_solid_color_texture(diffuse_color, "_base_color"), MaterialProperty::Type::Texture
                    );
                }
                material_asset->m_properties["base_tex"] = base_tex_prop;

                if (source_material.GetTextureCount(aiTextureType_METALNESS) > 0
                    || source_material.GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0
                    || source_material.GetTextureCount(aiTextureType_AMBIENT_OCCLUSION) > 0) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Material %s has PBR parameters, but Blinn-Phong path was selected.",
                        material_asset->m_name.c_str()
                    );
                }
            }

            material_refs[material_index] = AssetRef(material_asset->GetGUID());
            material_assets.emplace(material_index, material_asset);
        }

        SaveAsset(*db, *mesh_asset, path_in_project, mesh_asset->m_name);
        for (const auto &[_, material_asset] : material_assets) {
            SaveAsset(*db, *material_asset, path_in_project, material_asset->m_name);
        }
        for (const auto &texture_asset : texture_assets) {
            SaveAsset(*db, *texture_asset, path_in_project, texture_asset->m_name);
        }

        auto &temp_scene = MainClass::GetInstance()->GetWorldSystem()->CreateScene();
        auto &go = temp_scene.CreateGameObject();
        go.m_name = mesh_asset->m_name;
        auto &mesh_component = go.AddComponent<StaticMeshComponent>();
        mesh_component.m_mesh_asset = AssetRef(mesh_asset->GetGUID());
        for (uint32_t material_index : submesh_material_indices) {
            auto it = material_refs.find(material_index);
            mesh_component.m_material_assets.push_back(it == material_refs.end() ? default_blinn_material : it->second);
        }

        for (unsigned int light_index = 0; light_index < scene->mNumLights; ++light_index) {
            const aiLight *ai_light = scene->mLights[light_index];
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
                glm::vec3 direction{ai_light->mDirection.x, ai_light->mDirection.y, ai_light->mDirection.z};
                if (glm::length(direction) > 1e-5f) {
                    direction = glm::normalize(direction);
                    transform.SetRotation(glm::rotation(glm::vec3{0.0f, 1.0f, 0.0f}, direction));
                }
                light_go.SetTransform(transform);
            } else if (ai_light->mType == aiLightSource_POINT) {
                light_component.m_type = LightType::Point;
                transform.SetPosition(glm::vec3{ai_light->mPosition.x, ai_light->mPosition.y, ai_light->mPosition.z});
                light_go.SetTransform(transform);
            } else {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Light %s has unsupported type %d and will be skipped.",
                    ai_light->mName.C_Str(),
                    static_cast<int>(ai_light->mType)
                );
            }
        }

        temp_scene.FlushCmdQueue();

        auto scene_asset = std::make_unique<SceneAsset>();
        scene_asset->SaveFromScene(temp_scene);
        SaveAsset(*db, *scene_asset, path_in_project, "GO_" + mesh_asset->m_name);
    }
} // namespace Engine::detail
