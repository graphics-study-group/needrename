#include "GltfLoader.h"
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
#include <Framework/component/RenderComponent/StaticMeshComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>

#include <SDL3/SDL.h>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
    // Converts vectors from glTF basis into the engine basis.
    glm::vec3 ConvertGltfAxisToEngine(const glm::vec3 &value) {
        // glTF source basis is treated as right-handed Y-up and -Z forward.
        // Engine basis is right-handed Z-up and Y forward.
        return glm::vec3{value.x, -value.z, value.y};
    }

    // Position conversion entry point (currently basis conversion only).
    glm::vec3 TransformPosition(const glm::vec3 &value) {
        return ConvertGltfAxisToEngine(value);
    }

    // Direction conversion entry point that also normalizes non-degenerate vectors.
    glm::vec3 TransformDirection(const glm::vec3 &value) {
        glm::vec3 transformed = ConvertGltfAxisToEngine(value);
        if (glm::length(transformed) > 1e-6f) {
            transformed = glm::normalize(transformed);
        }
        return transformed;
    }

    // Build a stable tangent from normal when source mesh does not provide one.
    glm::vec4 BuildFallbackTangentFromNormal(const glm::vec3 &normal) {
        if (glm::length(normal) <= 1e-6f) {
            return glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};
        }

        const glm::vec3 n = glm::normalize(normal);
        const float abs_nz = n.z >= 0.0f ? n.z : -n.z;
        const glm::vec3 reference = abs_nz < 0.999f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};

        glm::vec3 tangent = glm::cross(reference, n);
        if (glm::length(tangent) <= 1e-6f) {
            tangent = glm::vec3{1.0f, 0.0f, 0.0f};
        } else {
            tangent = glm::normalize(tangent);
        }

        return glm::vec4{tangent, 1.0f};
    }

    // Returns the constant basis transform matrix used for glTF->engine matrix conversion.
    glm::mat4 GetBasisTransformMatrix() {
        static const glm::mat4 basis(
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f
        );
        return basis;
    }

    // Applies change-of-basis to a full TRS matrix.
    glm::mat4 ConvertTransformToEngine(const glm::mat4 &source_matrix) {
        const glm::mat4 basis = GetBasisTransformMatrix();
        const glm::mat4 basis_inv = glm::inverse(basis);
        return basis * source_matrix * basis_inv;
    }

    // Intermediate result for one glTF mesh import before runtime/persistence wiring.
    struct MeshImportOutput {
        Engine::MeshAsset *mesh_asset{nullptr};
        std::vector<std::optional<size_t>> submesh_material_indices{};
    };

    // Converts one glTF primitive into an engine submesh with packed vertex/index data.
    Engine::MeshAsset::Submesh BuildSubmeshFromPrimitive(
        const fastgltf::Asset &asset,
        const fastgltf::Primitive &primitive,
        const std::string &mesh_name,
        unsigned int primitive_index
    ) {
        const auto *position_it = primitive.findAttribute("POSITION");
        if (position_it == primitive.attributes.end()) {
            throw std::runtime_error("Primitive missing POSITION attribute.");
        }
        const auto &position_accessor = asset.accessors[position_it->accessorIndex];
        const size_t vertex_count = position_accessor.count;

        Engine::MeshAsset::Submesh submesh{};
        submesh.vertex_count = static_cast<uint32_t>(vertex_count);

        // Build staging arrays for every vertex stream used by the runtime mesh format.
        std::vector<float> positions(vertex_count * 3, 0.0f);
        std::vector<float> colors(vertex_count * 3, 1.0f);
        std::vector<float> normals(vertex_count * 3, 0.0f);
        std::vector<float> texcoord0(vertex_count * 2, 0.0f);
        std::vector<float> tangents;

        // Read and basis-convert POSITION into engine coordinates.
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, position_accessor, [&](auto pos, size_t idx) {
            const glm::vec3 transformed = TransformPosition(glm::vec3{pos.x(), pos.y(), pos.z()});
            positions[idx * 3 + 0] = transformed.x;
            positions[idx * 3 + 1] = transformed.y;
            positions[idx * 3 + 2] = transformed.z;
        });

        const auto *normal_it = primitive.findAttribute("NORMAL");
        if (normal_it == primitive.attributes.end()) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Primitive %u in mesh %s does not have normals and will be skipped.",
                primitive_index,
                mesh_name.c_str()
            );
            throw std::runtime_error("Primitive missing NORMAL attribute.");
        }

        const auto &normal_accessor = asset.accessors[normal_it->accessorIndex];
        // Read and basis-convert NORMAL; normals are mandatory in the current path.
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, normal_accessor, [&](auto n, size_t idx) {
            const glm::vec3 transformed = TransformDirection(glm::vec3{n.x(), n.y(), n.z()});
            normals[idx * 3 + 0] = transformed.x;
            normals[idx * 3 + 1] = transformed.y;
            normals[idx * 3 + 2] = transformed.z;
        });

        const auto *color_it = primitive.findAttribute("COLOR_0");
        if (color_it != primitive.attributes.end()) {
            const auto &color_accessor = asset.accessors[color_it->accessorIndex];
            if (color_accessor.type == fastgltf::AccessorType::Vec4) {
                // Vec4 vertex color: keep RGB and ignore alpha for current vertex format.
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                    asset,
                    color_accessor,
                    [&](auto c, size_t idx) {
                        colors[idx * 3 + 0] = c.x();
                        colors[idx * 3 + 1] = c.y();
                        colors[idx * 3 + 2] = c.z();
                    }
                );
            } else {
                // Vec3 vertex color: copy RGB directly.
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                    asset,
                    color_accessor,
                    [&](auto c, size_t idx) {
                        colors[idx * 3 + 0] = c.x();
                        colors[idx * 3 + 1] = c.y();
                        colors[idx * 3 + 2] = c.z();
                    }
                );
            }
        }

        const auto *uv_it = primitive.findAttribute("TEXCOORD_0");
        if (uv_it != primitive.attributes.end()) {
            const auto &uv_accessor = asset.accessors[uv_it->accessorIndex];
            // Import only UV0 because material binding currently supports a single UV set.
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, uv_accessor, [&](auto uv, size_t idx) {
                texcoord0[idx * 2 + 0] = uv.x();
                texcoord0[idx * 2 + 1] = uv.y();
            });
        }

        const auto *tangent_it = primitive.findAttribute("TANGENT");
        tangents.assign(vertex_count * 4, 0.0f);
        if (tangent_it != primitive.attributes.end()) {
            const auto &tangent_accessor = asset.accessors[tangent_it->accessorIndex];
            // Tangent xyz follows basis conversion; w keeps handedness sign from source.
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, tangent_accessor, [&](auto t, size_t idx) {
                const glm::vec3 transformed = TransformDirection(glm::vec3{t.x(), t.y(), t.z()});
                tangents[idx * 4 + 0] = transformed.x;
                tangents[idx * 4 + 1] = transformed.y;
                tangents[idx * 4 + 2] = transformed.z;
                tangents[idx * 4 + 3] = t.w();
            });
        } else {
            for (size_t idx = 0; idx < vertex_count; ++idx) {
                const glm::vec3 normal = glm::vec3{normals[idx * 3 + 0], normals[idx * 3 + 1], normals[idx * 3 + 2]};
                const glm::vec4 fallback_tangent = BuildFallbackTangentFromNormal(normal);
                tangents[idx * 4 + 0] = fallback_tangent.x;
                tangents[idx * 4 + 1] = fallback_tangent.y;
                tangents[idx * 4 + 2] = fallback_tangent.z;
                tangents[idx * 4 + 3] = fallback_tangent.w;
            }
        }

        if (primitive.indicesAccessor.has_value()) {
            const auto &index_accessor = asset.accessors[primitive.indicesAccessor.value()];
            submesh.m_indices.resize(index_accessor.count);
            fastgltf::copyFromAccessor<uint32_t>(asset, index_accessor, submesh.m_indices.data());
        } else {
            // Non-indexed primitive: synthesize a sequential index buffer.
            submesh.m_indices.resize(vertex_count);
            for (size_t i = 0; i < vertex_count; ++i) {
                submesh.m_indices[i] = static_cast<uint32_t>(i);
            }
        }

        size_t vertex_buffer_size = positions.size() * sizeof(float) + colors.size() * sizeof(float)
                                    + normals.size() * sizeof(float) + texcoord0.size() * sizeof(float);
        if (!tangents.empty()) {
            vertex_buffer_size += tangents.size() * sizeof(float);
        }
        submesh.m_vertex_attributes.reserve(vertex_buffer_size);

        // Pack attributes into one contiguous byte array and store layout metadata.
        submesh.positions.type = Engine::VertexAttributeType::SFloat32x3;
        Engine::detail::import_shared::AppendVertexAttribute(
            submesh.m_vertex_attributes, positions.data(), positions.size(), submesh.positions
        );

        submesh.color.type = Engine::VertexAttributeType::SFloat32x3;
        Engine::detail::import_shared::AppendVertexAttribute(
            submesh.m_vertex_attributes, colors.data(), colors.size(), submesh.color
        );

        submesh.normal.type = Engine::VertexAttributeType::SFloat32x3;
        Engine::detail::import_shared::AppendVertexAttribute(
            submesh.m_vertex_attributes, normals.data(), normals.size(), submesh.normal
        );

        submesh.texcoord0.type = Engine::VertexAttributeType::SFloat32x2;
        Engine::detail::import_shared::AppendVertexAttribute(
            submesh.m_vertex_attributes, texcoord0.data(), texcoord0.size(), submesh.texcoord0
        );

        submesh.tangent.type = Engine::VertexAttributeType::SFloat32x4;
        Engine::detail::import_shared::AppendVertexAttribute(
            submesh.m_vertex_attributes, tangents.data(), tangents.size(), submesh.tangent
        );

        return submesh;
    }

    // Imports all supported primitives from a glTF mesh and creates one MeshAsset.
    MeshImportOutput BuildMeshAssetFromGltfMesh(
        const fastgltf::Asset &asset, const fastgltf::Mesh &mesh, Engine::AssetManager &am, const std::string &mesh_name
    ) {
        MeshImportOutput output{};

        std::vector<Engine::MeshAsset::Submesh> imported_submeshes;
        std::vector<std::optional<size_t>> imported_material_indices;

        // Iterate each primitive and keep only successfully converted triangle primitives.
        for (size_t primitive_index = 0; primitive_index < mesh.primitives.size(); ++primitive_index) {
            const auto &primitive = mesh.primitives[primitive_index];
            if (primitive.type != fastgltf::PrimitiveType::Triangles) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Mesh %s primitive %u is not triangles and will be skipped.",
                    mesh_name.c_str(),
                    static_cast<unsigned int>(primitive_index)
                );
                continue;
            }

            try {
                // Convert primitive geometry and remember its optional material binding.
                imported_submeshes.push_back(
                    BuildSubmeshFromPrimitive(asset, primitive, mesh_name, static_cast<unsigned int>(primitive_index))
                );
                if (primitive.materialIndex.has_value()) {
                    imported_material_indices.push_back(static_cast<size_t>(primitive.materialIndex.value()));
                } else {
                    imported_material_indices.push_back(std::nullopt);
                }
            } catch (const std::exception &e) {
                // Keep importing remaining primitives even if one primitive is malformed.
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Skipping primitive %u in mesh %s: %s",
                    static_cast<unsigned int>(primitive_index),
                    mesh_name.c_str(),
                    e.what()
                );
            }
        }

        if (imported_submeshes.empty()) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Mesh %s has no supported triangle primitive and will be skipped.",
                mesh_name.c_str()
            );
            return output;
        }

        auto *mesh_asset = am.CreateAsset<Engine::MeshAsset>();
        mesh_asset->m_name = mesh_name;
        mesh_asset->m_submeshes = std::move(imported_submeshes);

        output.mesh_asset = mesh_asset;
        output.submesh_material_indices = std::move(imported_material_indices);

        return output;
    }

    // Builds node local transform from decomposed glTF TRS and converts basis to engine coordinates.
    Engine::Transform BuildNodeTransform(const fastgltf::Node &node) {
        if (!std::holds_alternative<fastgltf::TRS>(node.transform)) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Node uses matrix transform. Expected decomposed TRS. Falling back to identity transform."
            );
            return Engine::Transform{};
        }

        const auto &trs = std::get<fastgltf::TRS>(node.transform);
        // Recompose source TRS into matrix form so basis conversion is done in one step.
        glm::mat4 source_matrix(1.0f);
        const glm::vec3 source_translation{trs.translation.x(), trs.translation.y(), trs.translation.z()};
        const glm::quat source_rotation{trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z()};
        const glm::vec3 source_scale{trs.scale.x(), trs.scale.y(), trs.scale.z()};

        source_matrix = glm::translate(source_matrix, source_translation) * glm::mat4_cast(source_rotation)
                        * glm::scale(glm::mat4{1.0f}, source_scale);

        Engine::Transform transform{};
        transform.Decompose(ConvertTransformToEngine(source_matrix));
        return transform;
    }
} // namespace

namespace Engine {
    namespace {
        // Runtime wiring result for each imported mesh: mesh asset ref + ordered submesh material refs.
        struct MeshRuntimeOutput {
            AssetRef mesh_ref{};
            MeshAsset *mesh_asset{nullptr};
            std::vector<AssetRef> material_refs{};
        };

        // Main glTF import pipeline: parse, build assets, optionally persist, optionally build scene hierarchy.
        ImportResult ImportGltf(
            const std::filesystem::path &path,
            const std::optional<std::filesystem::path> &path_in_project,
            const std::weak_ptr<AssetManager> &asset_manager,
            const std::weak_ptr<FileSystemDatabase> &database,
            bool persist_assets,
            bool create_scene_asset
        ) {
            if (persist_assets && !path_in_project.has_value()) {
                throw std::invalid_argument("Persisted glTF import requires a valid path_in_project.");
            }

            auto db = database.lock();
            if (!db) {
                throw std::runtime_error("GltfLoader: AssetDatabase has been destroyed.");
            }
            auto am = asset_manager.lock();
            if (!am) {
                throw std::runtime_error("GltfLoader: AssetManager has been destroyed.");
            }

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering glTF loader: %s", path.string().c_str());

            // Stage 1: load raw file bytes and parse renderable glTF content.
            auto data_buffer = fastgltf::GltfDataBuffer::FromPath(path);
            if (!data_buffer) {
                throw std::runtime_error(
                    std::string("Failed to load glTF file bytes: ")
                    + fastgltf::getErrorMessage(data_buffer.error()).data()
                );
            }

            fastgltf::Parser parser(
                fastgltf::Extensions::KHR_lights_punctual | fastgltf::Extensions::KHR_materials_specular
                | fastgltf::Extensions::KHR_materials_ior | fastgltf::Extensions::KHR_materials_clearcoat
                | fastgltf::Extensions::KHR_materials_sheen | fastgltf::Extensions::KHR_materials_iridescence
                | fastgltf::Extensions::KHR_materials_transmission | fastgltf::Extensions::KHR_materials_anisotropy
                | fastgltf::Extensions::KHR_materials_diffuse_transmission
            );

            constexpr auto options = fastgltf::Options::DontRequireValidAssetMember
                                     | fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages
                                     | fastgltf::Options::DecomposeNodeMatrices
                                     | fastgltf::Options::GenerateMeshIndices;

            auto loaded_asset =
                parser.loadGltf(data_buffer.get(), path.parent_path(), options, fastgltf::Category::OnlyRenderable);
            if (loaded_asset.error() != fastgltf::Error::None) {
                throw std::runtime_error(
                    std::string("Failed to parse glTF: ") + fastgltf::getErrorMessage(loaded_asset.error()).data()
                );
            }

            const fastgltf::Asset &asset = loaded_asset.get();
            if (asset.meshes.empty()) {
                throw std::runtime_error("glTF file does not contain any mesh.");
            }

            // Stage 2: import geometry mesh-by-mesh and collect the material indices that are actually needed.
            std::unordered_map<std::string, uint32_t> name_counters;
            const std::string model_name = path.stem().string();

            std::vector<MeshImportOutput> mesh_imports(asset.meshes.size());
            std::vector<MeshRuntimeOutput> mesh_runtime(asset.meshes.size());
            std::unordered_set<size_t> required_material_indices;
            size_t valid_mesh_count = 0;

            for (size_t mesh_index = 0; mesh_index < asset.meshes.size(); ++mesh_index) {
                const auto &source_mesh = asset.meshes[mesh_index];
                const std::string fallback_mesh_name = model_name + "_mesh_" + std::to_string(mesh_index);
                const std::string mesh_name = Engine::detail::import_shared::MakeUniqueAssetName(
                    source_mesh.name.empty() ? fallback_mesh_name : std::string(source_mesh.name), name_counters
                );

                mesh_imports[mesh_index] = BuildMeshAssetFromGltfMesh(asset, source_mesh, *am, mesh_name);
                if (mesh_imports[mesh_index].mesh_asset == nullptr) {
                    continue;
                }

                ++valid_mesh_count;
                mesh_runtime[mesh_index].mesh_asset = mesh_imports[mesh_index].mesh_asset;
                mesh_runtime[mesh_index].mesh_ref = AssetRef(mesh_imports[mesh_index].mesh_asset->GetGUID());

                // Collect only referenced material indices to avoid creating unused materials.
                for (const auto &material_index : mesh_imports[mesh_index].submesh_material_indices) {
                    if (material_index.has_value()) {
                        required_material_indices.insert(material_index.value());
                    }
                }
            }

            if (valid_mesh_count == 0) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "glTF file %s has no supported triangle mesh primitive. Import will be skipped.",
                    path.string().c_str()
                );
                return ImportResult{};
            }

            // Stage 3: build textures/materials and then map submesh material slots to concrete material refs.
            Engine::detail::MaterialBuildOutput material_output = Engine::detail::BuildMaterialsFromGltf(
                asset, path, *am, *db, model_name, required_material_indices, name_counters
            );

            for (size_t mesh_index = 0; mesh_index < mesh_imports.size(); ++mesh_index) {
                auto &materials = mesh_runtime[mesh_index].material_refs;
                for (const auto &material_index : mesh_imports[mesh_index].submesh_material_indices) {
                    if (!material_index.has_value()) {
                        materials.push_back(material_output.default_pbr_material);
                        continue;
                    }
                    auto it = material_output.material_refs.find(material_index.value());
                    materials.push_back(
                        it == material_output.material_refs.end() ? material_output.default_pbr_material : it->second
                    );
                }
            }

            // Stage 4: produce return payload (primary mesh/material refs + created asset GUID lists).
            ImportResult result{};
            for (const auto &mesh_entry : mesh_runtime) {
                if (mesh_entry.mesh_ref.IsValid()) {
                    result.mesh_asset = mesh_entry.mesh_ref;
                    result.mesh_material_assets = mesh_entry.material_refs;
                    break;
                }
            }
            for (const auto *material_asset : material_output.created_material_assets) {
                result.created_material_assets.emplace_back(material_asset->GetGUID());
            }
            for (const auto *texture_asset : material_output.created_texture_assets) {
                result.created_texture_assets.emplace_back(texture_asset->GetGUID());
            }

            // Stage 5 (optional): persist imported assets to project asset database.
            if (persist_assets) {
                const auto &target_path = *path_in_project;
                for (const auto &mesh_entry : mesh_runtime) {
                    if (mesh_entry.mesh_asset != nullptr) {
                        Engine::detail::import_shared::SaveAsset(
                            *db, *mesh_entry.mesh_asset, target_path, mesh_entry.mesh_asset->m_name
                        );
                    }
                }
                for (const auto *material_asset : material_output.created_material_assets) {
                    Engine::detail::import_shared::SaveAsset(*db, *material_asset, target_path, material_asset->m_name);
                }
                for (const auto *texture_asset : material_output.created_texture_assets) {
                    Engine::detail::import_shared::SaveAsset(*db, *texture_asset, target_path, texture_asset->m_name);
                }
            }

            // Stage 6 (optional): recreate glTF node hierarchy into a temporary scene and save SceneAsset.
            if (create_scene_asset) {
                auto &temp_scene = MainClass::GetInstance()->GetWorldSystem()->CreateScene();

                // Determine scene roots from default scene; fall back to all nodes when scene metadata is absent.
                std::vector<size_t> scene_roots;
                if (!asset.scenes.empty()) {
                    const size_t scene_index =
                        asset.defaultScene.has_value() ? asset.defaultScene.value() : static_cast<size_t>(0);
                    if (scene_index < asset.scenes.size()) {
                        scene_roots = std::vector<size_t>(
                            asset.scenes[scene_index].nodeIndices.begin(), asset.scenes[scene_index].nodeIndices.end()
                        );
                    }
                }
                if (scene_roots.empty()) {
                    scene_roots.resize(asset.nodes.size());
                    for (size_t i = 0; i < asset.nodes.size(); ++i) {
                        scene_roots[i] = i;
                    }
                }

                // Mark all nodes reachable from roots so stray/disconnected nodes are ignored.
                std::vector<bool> included(asset.nodes.size(), false);
                std::vector<size_t> stack = scene_roots;
                while (!stack.empty()) {
                    const size_t node_index = stack.back();
                    stack.pop_back();
                    if (node_index >= asset.nodes.size() || included[node_index]) {
                        continue;
                    }
                    included[node_index] = true;
                    const auto &node = asset.nodes[node_index];
                    for (size_t child_index : node.children) {
                        stack.push_back(child_index);
                    }
                }

                // Create one GameObject per included node and apply converted local transform.
                std::vector<GameObject *> node_to_go(asset.nodes.size(), nullptr);
                for (size_t node_index = 0; node_index < asset.nodes.size(); ++node_index) {
                    if (!included[node_index]) {
                        continue;
                    }
                    auto &go = temp_scene.CreateGameObject();
                    const auto &node = asset.nodes[node_index];
                    go.m_name =
                        node.name.empty() ? model_name + "_node_" + std::to_string(node_index) : std::string(node.name);
                    go.SetTransform(BuildNodeTransform(node));
                    node_to_go[node_index] = &go;
                }

                // Rebuild parent-child relationships according to glTF node hierarchy.
                for (size_t node_index = 0; node_index < asset.nodes.size(); ++node_index) {
                    if (!included[node_index] || node_to_go[node_index] == nullptr) {
                        continue;
                    }
                    auto *parent_go = node_to_go[node_index];
                    for (size_t child_index : asset.nodes[node_index].children) {
                        if (child_index >= asset.nodes.size() || !included[child_index]
                            || node_to_go[child_index] == nullptr) {
                            continue;
                        }
                        node_to_go[child_index]->SetParent(parent_go->GetHandle());
                    }
                }

                // Attach StaticMeshComponent for nodes that reference successfully imported meshes.
                for (size_t node_index = 0; node_index < asset.nodes.size(); ++node_index) {
                    if (!included[node_index] || node_to_go[node_index] == nullptr) {
                        continue;
                    }
                    const auto &node = asset.nodes[node_index];
                    if (!node.meshIndex.has_value() || node.meshIndex.value() >= mesh_runtime.size()) {
                        continue;
                    }

                    const auto &mesh_entry = mesh_runtime[node.meshIndex.value()];
                    if (!mesh_entry.mesh_ref.IsValid()) {
                        continue;
                    }

                    auto &mesh_component = node_to_go[node_index]->AddComponent<StaticMeshComponent>();
                    mesh_component.m_mesh_asset = mesh_entry.mesh_ref;
                    mesh_component.m_material_assets = mesh_entry.material_refs;
                }

                if (!asset.lights.empty()) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "glTF has %u punctual light(s). Current glTF import keeps hierarchy only and does not "
                        "instantiate light components yet.",
                        static_cast<unsigned int>(asset.lights.size())
                    );
                }

                temp_scene.FlushCmdQueue();

                auto *scene_asset = am->CreateAsset<SceneAsset>();
                scene_asset->SaveFromScene(temp_scene);
                if (persist_assets) {
                    Engine::detail::import_shared::SaveAsset(*db, *scene_asset, *path_in_project, "GO_" + model_name);
                }
                result.scene_asset = AssetRef(scene_asset->GetGUID());
            }

            return result;
        }
    } // namespace

    // Captures shared services used by glTF import APIs.
    GltfLoader::GltfLoader() {
        m_asset_manager = MainClass::GetInstance()->GetAssetManager();
        m_database = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
    }

    // Public entry: import glTF and persist mesh/material/texture/scene assets into project database.
    void GltfLoader::LoadGltfResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project) {
        static_cast<void>(ImportGltf(path, path_in_project, m_asset_manager, m_database, true, true));
    }

    // Public entry: import glTF for runtime-only usage without writing assets to disk.
    ImportResult GltfLoader::LoadGltfInMemory(const std::filesystem::path &path) {
        return ImportGltf(path, std::nullopt, m_asset_manager, m_database, false, false);
    }
} // namespace Engine
