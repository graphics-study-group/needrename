#include "GltfLoader.h"

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/AssetManager/AssetManager.h>
#include <Asset/AssetRef.h>
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Mesh/MeshAsset.h>
#include <Asset/Scene/SceneAsset.h>
#include <Asset/Texture/Image2DTextureAsset.h>
#include <Asset/Texture/SolidColorTextureAsset.h>
#include <Core/Math/Transform.h>
#include <Framework/component/RenderComponent/StaticMeshComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Reflection/Archive.h>

#include <SDL3/SDL.h>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
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

    Engine::AssetPath MakeAssetPath(
        Engine::FileSystemDatabase &database,
        const std::filesystem::path &path_in_project,
        const std::string &asset_name
    ) {
        return Engine::AssetPath(database, path_in_project / (asset_name + ".asset"));
    }

    void SaveAsset(
        Engine::FileSystemDatabase &database,
        const Engine::Asset &asset,
        const std::filesystem::path &path_in_project,
        const std::string &asset_name
    ) {
        Engine::Serialization::Archive archive;
        archive.prepare_save();
        asset.save_asset_to_archive(archive);
        database.SaveArchive(archive, MakeAssetPath(database, path_in_project, asset_name));
    }

    glm::vec3 ConvertGltfAxisToEngine(const glm::vec3 &value) {
        // glTF source basis is treated as right-handed Y-up and -Z forward.
        // Engine basis is right-handed Z-up and Y forward.
        return glm::vec3{value.x, -value.z, value.y};
    }

    glm::vec3 TransformPosition(const glm::vec3 &value) {
        return ConvertGltfAxisToEngine(value);
    }

    glm::vec3 TransformDirection(const glm::vec3 &value) {
        glm::vec3 transformed = ConvertGltfAxisToEngine(value);
        if (glm::length(transformed) > 1e-6f) {
            transformed = glm::normalize(transformed);
        }
        return transformed;
    }

    glm::mat4 GetBasisTransformMatrix() {
        static const glm::mat4 basis(
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            -1.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f
        );
        return basis;
    }

    glm::mat4 ConvertTransformToEngine(const glm::mat4 &source_matrix) {
        const glm::mat4 basis = GetBasisTransformMatrix();
        const glm::mat4 basis_inv = glm::inverse(basis);
        return basis * source_matrix * basis_inv;
    }

    std::optional<std::filesystem::path> ResolveTexturePath(
        const std::filesystem::path &model_path, const fastgltf::URI &uri
    ) {
        if (!uri.isLocalPath()) {
            return std::nullopt;
        }

        const std::filesystem::path candidate_path = model_path.parent_path() / uri.fspath();
        std::error_code ec;
        if (std::filesystem::exists(candidate_path, ec)) {
            return std::filesystem::weakly_canonical(candidate_path, ec);
        }
        return std::nullopt;
    }

    struct ByteView {
        const std::byte *data{nullptr};
        size_t size{0};

        bool IsValid() const {
            return data != nullptr && size > 0;
        }
    };

    std::optional<ByteView> GetDataView(const fastgltf::DataSource &source) {
        return std::visit(
            fastgltf::visitor{
                [](const std::monostate &) -> std::optional<ByteView> { return std::nullopt; },
                [](const fastgltf::sources::BufferView &) -> std::optional<ByteView> { return std::nullopt; },
                [](const fastgltf::sources::URI &) -> std::optional<ByteView> { return std::nullopt; },
                [](const fastgltf::sources::Array &array_source) -> std::optional<ByteView> {
                    return ByteView{array_source.bytes.data(), array_source.bytes.size()};
                },
                [](const fastgltf::sources::Vector &vector_source) -> std::optional<ByteView> {
                    return ByteView{vector_source.bytes.data(), vector_source.bytes.size()};
                },
                [](const fastgltf::sources::CustomBuffer &) -> std::optional<ByteView> { return std::nullopt; },
                [](const fastgltf::sources::ByteView &byte_view_source) -> std::optional<ByteView> {
                    return ByteView{byte_view_source.bytes.data(), byte_view_source.bytes.size()};
                },
                [](const fastgltf::sources::Fallback &) -> std::optional<ByteView> { return std::nullopt; }
            },
            source
        );
    }

    struct MeshImportOutput {
        Engine::MeshAsset *mesh_asset{nullptr};
        std::vector<std::optional<size_t>> submesh_material_indices{};
    };

    struct MaterialBuildOutput {
        Engine::AssetRef default_pbr_material{};
        std::unordered_map<size_t, Engine::AssetRef> material_refs{};
        std::vector<Engine::MaterialAsset *> created_material_assets{};
        std::vector<Engine::TextureAsset *> created_texture_assets{};
    };

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

        std::vector<float> positions(vertex_count * 3, 0.0f);
        std::vector<float> colors(vertex_count * 3, 1.0f);
        std::vector<float> normals(vertex_count * 3, 0.0f);
        std::vector<float> texcoord0(vertex_count * 2, 0.0f);
        std::vector<float> tangents;

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
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, color_accessor, [&](auto c, size_t idx) {
                    colors[idx * 3 + 0] = c.x();
                    colors[idx * 3 + 1] = c.y();
                    colors[idx * 3 + 2] = c.z();
                });
            } else {
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(asset, color_accessor, [&](auto c, size_t idx) {
                    colors[idx * 3 + 0] = c.x();
                    colors[idx * 3 + 1] = c.y();
                    colors[idx * 3 + 2] = c.z();
                });
            }
        }

        const auto *uv_it = primitive.findAttribute("TEXCOORD_0");
        if (uv_it != primitive.attributes.end()) {
            const auto &uv_accessor = asset.accessors[uv_it->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(asset, uv_accessor, [&](auto uv, size_t idx) {
                texcoord0[idx * 2 + 0] = uv.x();
                texcoord0[idx * 2 + 1] = uv.y();
            });
        }

        const auto *tangent_it = primitive.findAttribute("TANGENT");
        if (tangent_it != primitive.attributes.end()) {
            tangents.assign(vertex_count * 4, 0.0f);
            const auto &tangent_accessor = asset.accessors[tangent_it->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(asset, tangent_accessor, [&](auto t, size_t idx) {
                const glm::vec3 transformed = TransformDirection(glm::vec3{t.x(), t.y(), t.z()});
                tangents[idx * 4 + 0] = transformed.x;
                tangents[idx * 4 + 1] = transformed.y;
                tangents[idx * 4 + 2] = transformed.z;
                tangents[idx * 4 + 3] = t.w();
            });
        }

        if (primitive.indicesAccessor.has_value()) {
            const auto &index_accessor = asset.accessors[primitive.indicesAccessor.value()];
            submesh.m_indices.resize(index_accessor.count);
            fastgltf::copyFromAccessor<uint32_t>(asset, index_accessor, submesh.m_indices.data());
        } else {
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

        submesh.positions.type = Engine::VertexAttributeType::SFloat32x3;
        AppendVertexAttribute(submesh.m_vertex_attributes, positions.data(), positions.size(), submesh.positions);

        submesh.color.type = Engine::VertexAttributeType::SFloat32x3;
        AppendVertexAttribute(submesh.m_vertex_attributes, colors.data(), colors.size(), submesh.color);

        submesh.normal.type = Engine::VertexAttributeType::SFloat32x3;
        AppendVertexAttribute(submesh.m_vertex_attributes, normals.data(), normals.size(), submesh.normal);

        if (!tangents.empty()) {
            submesh.tangent.type = Engine::VertexAttributeType::SFloat32x4;
            AppendVertexAttribute(submesh.m_vertex_attributes, tangents.data(), tangents.size(), submesh.tangent);
        }

        submesh.texcoord0.type = Engine::VertexAttributeType::SFloat32x2;
        AppendVertexAttribute(submesh.m_vertex_attributes, texcoord0.data(), texcoord0.size(), submesh.texcoord0);

        return submesh;
    }

    MeshImportOutput BuildMeshAssetFromGltfMesh(
        const fastgltf::Asset &asset,
        const fastgltf::Mesh &mesh,
        Engine::AssetManager &am,
        const std::string &mesh_name
    ) {
        MeshImportOutput output{};

        std::vector<Engine::MeshAsset::Submesh> imported_submeshes;
        std::vector<std::optional<size_t>> imported_material_indices;

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
                imported_submeshes.push_back(
                    BuildSubmeshFromPrimitive(asset, primitive, mesh_name, static_cast<unsigned int>(primitive_index))
                );
                if (primitive.materialIndex.has_value()) {
                    imported_material_indices.push_back(static_cast<size_t>(primitive.materialIndex.value()));
                } else {
                    imported_material_indices.push_back(std::nullopt);
                }
            } catch (const std::exception &e) {
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

    std::optional<Engine::AssetRef> BuildTextureAssetFromImage(
        const fastgltf::Asset &asset,
        const fastgltf::Image &image,
        size_t image_index,
        const std::filesystem::path &model_path,
        Engine::AssetManager &am,
        std::unordered_map<std::string, uint32_t> &name_counters,
        const std::string &model_name,
        std::vector<Engine::TextureAsset *> &created_texture_assets
    ) {
        auto *image_texture = am.CreateAsset<Engine::Image2DTextureAsset>();
        image_texture->m_name = MakeUniqueAssetName(model_name + "_image_" + std::to_string(image_index), name_counters);

        bool loaded = false;

        std::visit(
            fastgltf::visitor{
                [&](const std::monostate &) {},
                [&](const fastgltf::sources::URI &uri_source) {
                    auto resolved_path = ResolveTexturePath(model_path, uri_source.uri);
                    if (!resolved_path) {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "Failed to resolve external texture URI for image %u.",
                            static_cast<unsigned int>(image_index)
                        );
                        return;
                    }
                    image_texture->LoadFromFile(*resolved_path);
                    loaded = true;
                },
                [&](const fastgltf::sources::Array &array_source) {
                    image_texture->LoadFromMemory(array_source.bytes.data(), array_source.bytes.size());
                    loaded = true;
                },
                [&](const fastgltf::sources::Vector &vector_source) {
                    image_texture->LoadFromMemory(vector_source.bytes.data(), vector_source.bytes.size());
                    loaded = true;
                },
                [&](const fastgltf::sources::ByteView &byte_view_source) {
                    image_texture->LoadFromMemory(byte_view_source.bytes.data(), byte_view_source.bytes.size());
                    loaded = true;
                },
                [&](const fastgltf::sources::BufferView &buffer_view_source) {
                    if (buffer_view_source.bufferViewIndex >= asset.bufferViews.size()) {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "Image %u references invalid bufferView %u.",
                            static_cast<unsigned int>(image_index),
                            static_cast<unsigned int>(buffer_view_source.bufferViewIndex)
                        );
                        return;
                    }
                    const auto &buffer_view = asset.bufferViews[buffer_view_source.bufferViewIndex];
                    if (buffer_view.bufferIndex >= asset.buffers.size()) {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "Image %u bufferView references invalid buffer %u.",
                            static_cast<unsigned int>(image_index),
                            static_cast<unsigned int>(buffer_view.bufferIndex)
                        );
                        return;
                    }
                    const auto &buffer = asset.buffers[buffer_view.bufferIndex];
                    auto buffer_data = GetDataView(buffer.data);
                    if (!buffer_data.has_value() || !buffer_data->IsValid()) {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "Buffer data for image %u is unavailable.",
                            static_cast<unsigned int>(image_index)
                        );
                        return;
                    }
                    if (buffer_view.byteOffset + buffer_view.byteLength > buffer_data->size) {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "Image %u bufferView range is out of bounds.",
                            static_cast<unsigned int>(image_index)
                        );
                        return;
                    }
                    const std::byte *begin = buffer_data->data + buffer_view.byteOffset;
                    image_texture->LoadFromMemory(begin, buffer_view.byteLength);
                    loaded = true;
                },
                [&](const fastgltf::sources::CustomBuffer &) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Image %u uses CustomBuffer source which is not supported.",
                        static_cast<unsigned int>(image_index)
                    );
                },
                [&](const fastgltf::sources::Fallback &) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Image %u uses fallback source and will be skipped.",
                        static_cast<unsigned int>(image_index)
                    );
                }
            },
            image.data
        );

        if (!loaded) {
            return std::nullopt;
        }

        created_texture_assets.push_back(image_texture);
        return Engine::AssetRef(image_texture->GetGUID());
    }

    MaterialBuildOutput BuildMaterials(
        const fastgltf::Asset &asset,
        const std::filesystem::path &path,
        Engine::AssetManager &am,
        Engine::FileSystemDatabase &db,
        const std::string &model_name,
        const std::unordered_set<size_t> &required_material_indices,
        std::unordered_map<std::string, uint32_t> &name_counters
    ) {
        MaterialBuildOutput output{};
        std::unordered_map<size_t, Engine::AssetRef> texture_refs_by_image;

        const Engine::AssetRef default_pbr_albedo =
            db.GetNewAssetRef(Engine::AssetPath(db, std::filesystem::path("~/textures/dark_grey.asset")));
        const Engine::AssetRef default_pbr_mrao =
            db.GetNewAssetRef(Engine::AssetPath(db, std::filesystem::path("~/textures/white.asset")));

        auto *default_material = am.CreateAsset<Engine::MaterialAsset>();
        default_material->m_name = MakeUniqueAssetName(model_name + "_default_pbr", name_counters);
        default_material->m_library = db.GetNewAssetRef(
            Engine::AssetPath(db, std::filesystem::path("~/material_libraries/PBRLibrary.asset"))
        );
        default_material->m_properties["albedoSampler"] =
            Engine::MaterialProperty(default_pbr_albedo, Engine::MaterialProperty::Type::Texture);
        default_material->m_properties["MRAOSampler"] =
            Engine::MaterialProperty(default_pbr_mrao, Engine::MaterialProperty::Type::Texture);
        default_material->m_properties["metalness_scale"] = 1.0f;
        default_material->m_properties["roughness_scale"] = 1.0f;
        output.created_material_assets.push_back(default_material);
        output.default_pbr_material = Engine::AssetRef(default_material->GetGUID());

        auto create_solid_color_texture = [&](const glm::vec4 &color, const std::string &name_suffix) -> Engine::AssetRef {
            auto *solid_color = am.CreateAsset<Engine::SolidColorTextureAsset>();
            solid_color->m_name = MakeUniqueAssetName(model_name + name_suffix, name_counters);
            solid_color->m_color = color;
            output.created_texture_assets.push_back(solid_color);
            return Engine::AssetRef(solid_color->GetGUID());
        };

        auto try_load_texture_ref = [&](const fastgltf::Optional<fastgltf::TextureInfo> &texture_info,
                                        Engine::MaterialProperty &out_prop,
                                        const char *purpose) -> bool {
            if (!texture_info.has_value()) {
                return false;
            }

            size_t texcoord_index = texture_info->texCoordIndex;
            if (texture_info->transform && texture_info->transform->texCoordIndex.has_value()) {
                texcoord_index = texture_info->transform->texCoordIndex.value();
            }
            if (texcoord_index != 0) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Texture %s uses UV set %u, but only UV0 is supported.",
                    purpose,
                    static_cast<unsigned int>(texcoord_index)
                );
                return false;
            }

            if (texture_info->textureIndex >= asset.textures.size()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Texture index %u for %s is out of range.",
                    static_cast<unsigned int>(texture_info->textureIndex),
                    purpose
                );
                return false;
            }

            const auto &texture = asset.textures[texture_info->textureIndex];
            if (!texture.imageIndex.has_value()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Texture %u for %s does not provide a regular image source.",
                    static_cast<unsigned int>(texture_info->textureIndex),
                    purpose
                );
                return false;
            }

            const size_t image_index = texture.imageIndex.value();
            auto cached = texture_refs_by_image.find(image_index);
            if (cached != texture_refs_by_image.end()) {
                out_prop = Engine::MaterialProperty(cached->second, Engine::MaterialProperty::Type::Texture);
                return true;
            }

            if (image_index >= asset.images.size()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Image index %u for %s is out of range.",
                    static_cast<unsigned int>(image_index),
                    purpose
                );
                return false;
            }

            auto texture_ref = BuildTextureAssetFromImage(
                asset,
                asset.images[image_index],
                image_index,
                path,
                am,
                name_counters,
                model_name,
                output.created_texture_assets
            );
            if (!texture_ref.has_value()) {
                return false;
            }

            texture_refs_by_image.emplace(image_index, texture_ref.value());
            out_prop = Engine::MaterialProperty(texture_ref.value(), Engine::MaterialProperty::Type::Texture);
            return true;
        };

        for (size_t material_index : required_material_indices) {
            if (material_index >= asset.materials.size()) {
                output.material_refs[material_index] = output.default_pbr_material;
                continue;
            }

            const auto &source_material = asset.materials[material_index];
            auto *material_asset = am.CreateAsset<Engine::MaterialAsset>();
            const std::string fallback_name = model_name + "_material_" + std::to_string(material_index);
            const std::string base_name =
                source_material.name.empty() ? fallback_name : std::string(source_material.name);
            material_asset->m_name = MakeUniqueAssetName(base_name, name_counters);
            material_asset->m_library = db.GetNewAssetRef(
                Engine::AssetPath(db, std::filesystem::path("~/material_libraries/PBRLibrary.asset"))
            );

            const glm::vec4 base_color_factor = glm::vec4{
                source_material.pbrData.baseColorFactor.x(),
                source_material.pbrData.baseColorFactor.y(),
                source_material.pbrData.baseColorFactor.z(),
                source_material.pbrData.baseColorFactor.w()
            };

            Engine::MaterialProperty albedo_prop;
            if (!try_load_texture_ref(source_material.pbrData.baseColorTexture, albedo_prop, "baseColorTexture")) {
                const bool is_default_base = glm::all(glm::epsilonEqual(base_color_factor, glm::vec4{1.0f}, 1e-5f));
                const Engine::AssetRef albedo_ref =
                    is_default_base ? default_pbr_albedo : create_solid_color_texture(base_color_factor, "_albedo");
                albedo_prop = Engine::MaterialProperty(albedo_ref, Engine::MaterialProperty::Type::Texture);
            }
            material_asset->m_properties["albedoSampler"] = albedo_prop;

            Engine::MaterialProperty mrao_prop;
            const bool has_mrao =
                try_load_texture_ref(source_material.pbrData.metallicRoughnessTexture, mrao_prop, "metallicRoughnessTexture");
            material_asset->m_properties["MRAOSampler"] = has_mrao
                                                                ? mrao_prop
                                                                : Engine::MaterialProperty(
                                                                      default_pbr_mrao,
                                                                      Engine::MaterialProperty::Type::Texture
                                                                  );

            if (source_material.occlusionTexture.has_value()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Material %s has separate occlusionTexture. Current PBR path cannot merge AO into MRAO and will use fallback AO.",
                    material_asset->m_name.c_str()
                );
            }

            material_asset->m_properties["metalness_scale"] = static_cast<float>(source_material.pbrData.metallicFactor);
            material_asset->m_properties["roughness_scale"] = static_cast<float>(source_material.pbrData.roughnessFactor);

            if (source_material.normalTexture.has_value()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Material %s has normalTexture, but current PBR path does not support it yet.",
                    material_asset->m_name.c_str()
                );
            }
            if (source_material.emissiveTexture.has_value()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Material %s has emissiveTexture, but current PBR path does not support it yet.",
                    material_asset->m_name.c_str()
                );
            }
            if (source_material.clearcoat || source_material.specular || source_material.transmission
                || source_material.sheen || source_material.iridescence || source_material.anisotropy
                || source_material.specularGlossiness || source_material.volume || source_material.diffuseTransmission) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Material %s contains advanced PBR extensions that are not supported in current runtime path.",
                    material_asset->m_name.c_str()
                );
            }

            output.material_refs[material_index] = Engine::AssetRef(material_asset->GetGUID());
            output.created_material_assets.push_back(material_asset);
        }

        return output;
    }

    Engine::Transform BuildNodeTransform(const fastgltf::Node &node) {
        if (!std::holds_alternative<fastgltf::TRS>(node.transform)) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Node uses matrix transform. Expected decomposed TRS. Falling back to identity transform."
            );
            return Engine::Transform{};
        }

        const auto &trs = std::get<fastgltf::TRS>(node.transform);
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
        struct MeshRuntimeOutput {
            AssetRef mesh_ref{};
            MeshAsset *mesh_asset{nullptr};
            std::vector<AssetRef> material_refs{};
        };

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
            assert(db);
            auto am = asset_manager.lock();
            assert(am);

            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Entering glTF loader: %s", path.string().c_str());

            auto data_buffer = fastgltf::GltfDataBuffer::FromPath(path);
            if (!data_buffer) {
                throw std::runtime_error(std::string("Failed to load glTF file bytes: ")
                                         + fastgltf::getErrorMessage(data_buffer.error()).data());
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

            auto loaded_asset = parser.loadGltf(
                data_buffer.get(), path.parent_path(), options, fastgltf::Category::OnlyRenderable
            );
            if (loaded_asset.error() != fastgltf::Error::None) {
                throw std::runtime_error(
                    std::string("Failed to parse glTF: ") + fastgltf::getErrorMessage(loaded_asset.error()).data()
                );
            }

            const fastgltf::Asset &asset = loaded_asset.get();
            if (asset.meshes.empty()) {
                throw std::runtime_error("glTF file does not contain any mesh.");
            }

            std::unordered_map<std::string, uint32_t> name_counters;
            const std::string model_name = path.stem().string();

            std::vector<MeshImportOutput> mesh_imports(asset.meshes.size());
            std::vector<MeshRuntimeOutput> mesh_runtime(asset.meshes.size());
            std::unordered_set<size_t> required_material_indices;
            size_t valid_mesh_count = 0;

            for (size_t mesh_index = 0; mesh_index < asset.meshes.size(); ++mesh_index) {
                const auto &source_mesh = asset.meshes[mesh_index];
                const std::string fallback_mesh_name = model_name + "_mesh_" + std::to_string(mesh_index);
                const std::string mesh_name = MakeUniqueAssetName(
                    source_mesh.name.empty() ? fallback_mesh_name : std::string(source_mesh.name),
                    name_counters
                );

                mesh_imports[mesh_index] = BuildMeshAssetFromGltfMesh(asset, source_mesh, *am, mesh_name);
                if (mesh_imports[mesh_index].mesh_asset == nullptr) {
                    continue;
                }

                ++valid_mesh_count;
                mesh_runtime[mesh_index].mesh_asset = mesh_imports[mesh_index].mesh_asset;
                mesh_runtime[mesh_index].mesh_ref = AssetRef(mesh_imports[mesh_index].mesh_asset->GetGUID());

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

            MaterialBuildOutput material_output =
                BuildMaterials(asset, path, *am, *db, model_name, required_material_indices, name_counters);

            for (size_t mesh_index = 0; mesh_index < mesh_imports.size(); ++mesh_index) {
                auto &materials = mesh_runtime[mesh_index].material_refs;
                for (const auto &material_index : mesh_imports[mesh_index].submesh_material_indices) {
                    if (!material_index.has_value()) {
                        materials.push_back(material_output.default_pbr_material);
                        continue;
                    }
                    auto it = material_output.material_refs.find(material_index.value());
                    materials.push_back(it == material_output.material_refs.end() ? material_output.default_pbr_material
                                                                                  : it->second);
                }
            }

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

            if (persist_assets) {
                const auto &target_path = *path_in_project;
                for (const auto &mesh_entry : mesh_runtime) {
                    if (mesh_entry.mesh_asset != nullptr) {
                        SaveAsset(*db, *mesh_entry.mesh_asset, target_path, mesh_entry.mesh_asset->m_name);
                    }
                }
                for (const auto *material_asset : material_output.created_material_assets) {
                    SaveAsset(*db, *material_asset, target_path, material_asset->m_name);
                }
                for (const auto *texture_asset : material_output.created_texture_assets) {
                    SaveAsset(*db, *texture_asset, target_path, texture_asset->m_name);
                }
            }

            if (create_scene_asset) {
                auto &temp_scene = MainClass::GetInstance()->GetWorldSystem()->CreateScene();

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

                std::vector<GameObject *> node_to_go(asset.nodes.size(), nullptr);
                for (size_t node_index = 0; node_index < asset.nodes.size(); ++node_index) {
                    if (!included[node_index]) {
                        continue;
                    }
                    auto &go = temp_scene.CreateGameObject();
                    const auto &node = asset.nodes[node_index];
                    go.m_name = node.name.empty() ? model_name + "_node_" + std::to_string(node_index)
                                                  : std::string(node.name);
                    go.SetTransform(BuildNodeTransform(node));
                    node_to_go[node_index] = &go;
                }

                for (size_t node_index = 0; node_index < asset.nodes.size(); ++node_index) {
                    if (!included[node_index] || node_to_go[node_index] == nullptr) {
                        continue;
                    }
                    auto *parent_go = node_to_go[node_index];
                    for (size_t child_index : asset.nodes[node_index].children) {
                        if (child_index >= asset.nodes.size() || !included[child_index] || node_to_go[child_index] == nullptr) {
                            continue;
                        }
                        node_to_go[child_index]->SetParent(parent_go->GetHandle());
                    }
                }

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
                        "glTF has %u punctual light(s). Current glTF import keeps hierarchy only and does not instantiate light components yet.",
                        static_cast<unsigned int>(asset.lights.size())
                    );
                }

                temp_scene.FlushCmdQueue();

                auto *scene_asset = am->CreateAsset<SceneAsset>();
                scene_asset->SaveFromScene(temp_scene);
                if (persist_assets) {
                    SaveAsset(*db, *scene_asset, *path_in_project, "GO_" + model_name);
                }
                result.scene_asset = AssetRef(scene_asset->GetGUID());
            }

            return result;
        }
    } // namespace

    GltfLoader::GltfLoader() {
        m_asset_manager = MainClass::GetInstance()->GetAssetManager();
        m_database = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
    }

    void GltfLoader::LoadGltfResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project) {
        static_cast<void>(ImportGltf(path, path_in_project, m_asset_manager, m_database, true, true));
    }

    ImportResult GltfLoader::LoadGltfInMemory(const std::filesystem::path &path) {
        return ImportGltf(path, std::nullopt, m_asset_manager, m_database, false, false);
    }
} // namespace Engine
