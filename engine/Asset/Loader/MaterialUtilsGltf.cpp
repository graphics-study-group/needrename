#include "ImportSharedUtil.h"
#include "MaterialUtils.h"

#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Asset/Texture/SolidColorTextureAsset.h"

#include <SDL3/SDL.h>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <cmath>
#include <optional>
#include <variant>

namespace Engine::detail {
    namespace {
        // Resolve glTF URI to local filesystem path when URI points to external file.
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

        // Lightweight non-owning byte range for fastgltf source payload access.
        struct ByteView {
            const std::byte *data{nullptr};
            size_t size{0};

            // Validate that current view points to non-empty readable data.
            bool IsValid() const {
                return data != nullptr && size > 0;
            }
        };

        // Extract in-memory byte payload from fastgltf data source variants.
        std::optional<ByteView> GetDataView(const fastgltf::DataSource &source) {
            return std::visit(
                fastgltf::visitor{
                    // Variants without direct in-memory byte access.
                    [](const std::monostate &) -> std::optional<ByteView> { return std::nullopt; },
                    [](const fastgltf::sources::BufferView &) -> std::optional<ByteView> { return std::nullopt; },
                    [](const fastgltf::sources::URI &) -> std::optional<ByteView> { return std::nullopt; },
                    // Variants with direct in-memory byte access.
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

        // Build one texture asset from glTF image payload (external or embedded).
        std::optional<AssetRef> BuildTextureAssetFromImage(
            const fastgltf::Asset &asset,
            const fastgltf::Image &image,
            size_t image_index,
            const std::filesystem::path &model_path,
            AssetManager &am,
            std::unordered_map<std::string, uint32_t> &name_counters,
            const std::string &model_name,
            std::vector<TextureAsset *> &created_texture_assets
        ) {
            auto *image_texture = am.CreateAsset<Image2DTextureAsset>();
            image_texture->m_name =
                import_shared::MakeUniqueAssetName(model_name + "_image_" + std::to_string(image_index), name_counters);

            // Track whether any source branch successfully decoded texture bytes.
            bool loaded = false;

            // Dispatch by glTF image source type and decode into Image2DTextureAsset.
            std::visit(
                fastgltf::visitor{
                    [&](const std::monostate &) {},
                    [&](const fastgltf::sources::URI &uri_source) {
                        // External URI source path branch.
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
                        // Embedded byte array branch.
                        image_texture->LoadFromMemory(array_source.bytes.data(), array_source.bytes.size());
                        loaded = true;
                    },
                    [&](const fastgltf::sources::Vector &vector_source) {
                        // Embedded byte vector branch.
                        image_texture->LoadFromMemory(vector_source.bytes.data(), vector_source.bytes.size());
                        loaded = true;
                    },
                    [&](const fastgltf::sources::ByteView &byte_view_source) {
                        // Embedded byte-view branch.
                        image_texture->LoadFromMemory(byte_view_source.bytes.data(), byte_view_source.bytes.size());
                        loaded = true;
                    },
                    [&](const fastgltf::sources::BufferView &buffer_view_source) {
                        // BufferView branch: validate indices/range then decode bytes.
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
                        // Current importer path does not support custom buffer callbacks.
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "Image %u uses CustomBuffer source which is not supported.",
                            static_cast<unsigned int>(image_index)
                        );
                    },
                    [&](const fastgltf::sources::Fallback &) {
                        // Fallback source cannot provide deterministic texture bytes in current path.
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
            return AssetRef(image_texture->GetGUID());
        }
    } // namespace

    // Translate glTF material table into runtime PBR material/texture assets.
    MaterialBuildOutput BuildMaterialsFromGltf(
        const fastgltf::Asset &asset,
        const std::filesystem::path &path,
        AssetManager &am,
        FileSystemDatabase &db,
        const std::string &model_name,
        const std::unordered_set<size_t> &required_material_indices,
        std::unordered_map<std::string, uint32_t> &name_counters
    ) {
        MaterialBuildOutput output{};
        std::unordered_map<size_t, AssetRef> texture_refs_by_image;

        const AssetRef default_pbr_albedo =
            db.GetNewAssetRef(AssetPath(db, std::filesystem::path("~/textures/dark_grey.asset")));
        const AssetRef default_pbr_mrao =
            db.GetNewAssetRef(AssetPath(db, std::filesystem::path("~/textures/white.asset")));

        auto *default_material = am.CreateAsset<MaterialAsset>();
        default_material->m_name = import_shared::MakeUniqueAssetName(model_name + "_default_pbr", name_counters);
        default_material->m_library =
            db.GetNewAssetRef(AssetPath(db, std::filesystem::path("~/material_libraries/PBRLibrary.asset")));
        default_material->m_properties["albedoSampler"] =
            MaterialProperty(default_pbr_albedo, MaterialProperty::Type::Texture);
        default_material->m_properties["MRAOSampler"] =
            MaterialProperty(default_pbr_mrao, MaterialProperty::Type::Texture);
        default_material->m_properties["metalness_scale"] = 1.0f;
        default_material->m_properties["roughness_scale"] = 1.0f;
        output.created_material_assets.push_back(default_material);
        output.default_pbr_material = AssetRef(default_material->GetGUID());

        // Lambda: create a solid-color texture fallback from baseColorFactor when albedo map is missing.
        auto create_solid_color_texture = [&](const glm::vec4 &color, const std::string &name_suffix) -> AssetRef {
            auto *solid_color = am.CreateAsset<SolidColorTextureAsset>();
            solid_color->m_name = import_shared::MakeUniqueAssetName(model_name + name_suffix, name_counters);
            solid_color->m_color = color;
            output.created_texture_assets.push_back(solid_color);
            return AssetRef(solid_color->GetGUID());
        };

        // Lambda: validate glTF texture info, enforce UV0, then load/cache image-backed texture.
        auto try_load_texture_ref = [&](const fastgltf::Optional<fastgltf::TextureInfo> &texture_info,
                                        MaterialProperty &out_prop,
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
                out_prop = MaterialProperty(cached->second, MaterialProperty::Type::Texture);
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
            out_prop = MaterialProperty(texture_ref.value(), MaterialProperty::Type::Texture);
            return true;
        };

        // Iterate referenced glTF materials and build corresponding runtime PBR materials.
        for (size_t material_index : required_material_indices) {
            if (material_index >= asset.materials.size()) {
                output.material_refs[material_index] = output.default_pbr_material;
                continue;
            }

            const auto &source_material = asset.materials[material_index];
            auto *material_asset = am.CreateAsset<MaterialAsset>();
            const std::string fallback_name = model_name + "_material_" + std::to_string(material_index);
            const std::string base_name =
                source_material.name.empty() ? fallback_name : std::string(source_material.name);
            material_asset->m_name = import_shared::MakeUniqueAssetName(base_name, name_counters);
            material_asset->m_library =
                db.GetNewAssetRef(AssetPath(db, std::filesystem::path("~/material_libraries/PBRLibrary.asset")));

            const glm::vec4 base_color_factor = glm::vec4{
                source_material.pbrData.baseColorFactor.x(),
                source_material.pbrData.baseColorFactor.y(),
                source_material.pbrData.baseColorFactor.z(),
                source_material.pbrData.baseColorFactor.w()
            };

            MaterialProperty albedo_prop;
            if (!try_load_texture_ref(source_material.pbrData.baseColorTexture, albedo_prop, "baseColorTexture")) {
                // Albedo fallback branch: default builtin texture or generated solid color texture.
                const float eps = 1e-5f;
                const bool is_default_base =
                    std::fabs(base_color_factor.x - 1.0f) <= eps && std::fabs(base_color_factor.y - 1.0f) <= eps
                    && std::fabs(base_color_factor.z - 1.0f) <= eps && std::fabs(base_color_factor.w - 1.0f) <= eps;
                const AssetRef albedo_ref =
                    is_default_base ? default_pbr_albedo : create_solid_color_texture(base_color_factor, "_albedo");
                albedo_prop = MaterialProperty(albedo_ref, MaterialProperty::Type::Texture);
            }
            material_asset->m_properties["albedoSampler"] = albedo_prop;

            MaterialProperty mrao_prop;
            const bool has_mrao = try_load_texture_ref(
                source_material.pbrData.metallicRoughnessTexture, mrao_prop, "metallicRoughnessTexture"
            );
            // MRAO fallback branch: use builtin white texture when map is missing.
            material_asset->m_properties["MRAOSampler"] =
                has_mrao ? mrao_prop : MaterialProperty(default_pbr_mrao, MaterialProperty::Type::Texture);

            if (source_material.occlusionTexture.has_value()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Material %s has separate occlusionTexture. Current PBR path cannot merge AO into MRAO and will "
                    "use fallback AO.",
                    material_asset->m_name.c_str()
                );
            }

            material_asset->m_properties["metalness_scale"] =
                static_cast<float>(source_material.pbrData.metallicFactor);
            material_asset->m_properties["roughness_scale"] =
                static_cast<float>(source_material.pbrData.roughnessFactor);

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
                || source_material.specularGlossiness || source_material.volume
                || source_material.diffuseTransmission) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Material %s contains advanced PBR extensions that are not supported in current runtime path.",
                    material_asset->m_name.c_str()
                );
            }

            output.material_refs[material_index] = AssetRef(material_asset->GetGUID());
            output.created_material_assets.push_back(material_asset);
        }

        return output;
    }
} // namespace Engine::detail
