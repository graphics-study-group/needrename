#include "ImportSharedUtil.h"
#include "MaterialUtils.h"

#include "Asset/AssetDatabase/FileSystemDatabase.h"
#include "Asset/AssetManager/AssetManager.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"
#include "Asset/Texture/SolidColorTextureAsset.h"

#include <SDL3/SDL.h>
#include <assimp/material.h>
#include <assimp/scene.h>

#include <cmath>
#include <optional>
#include <variant>

namespace Engine::detail {
    namespace {
        // Convert Assimp color value to engine glm color value.
        glm::vec4 ToGlm(const aiColor4D &color) {
            return glm::vec4{color.r, color.g, color.b, color.a};
        }

        // Resolve Assimp texture string to an existing local file path.
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
            // Try direct path, model-relative path and filename fallback in order.
            for (const auto &candidate : candidates) {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec)) {
                    return std::filesystem::weakly_canonical(candidate, ec);
                }
            }
            return std::nullopt;
        }

        // Helper for PBR-heuristic texture slot detection.
        bool HasTextureType(const aiMaterial &material, aiTextureType texture_type) {
            return material.GetTextureCount(texture_type) > 0;
        }

        // Decide whether an Assimp material should be translated through PBR pipeline.
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

    // Translate Assimp material table into runtime material/texture assets.
    MaterialBuildOutput BuildMaterialsFromAssimp(
        const aiScene &scene,
        const std::filesystem::path &path,
        AssetManager &am,
        FileSystemDatabase &db,
        const std::string &model_name,
        const std::vector<uint32_t> &submesh_material_indices,
        std::unordered_map<std::string, uint32_t> &name_counters
    ) {
        MaterialBuildOutput output{};
        std::unordered_map<std::string, AssetRef> texture_refs_by_path;
        std::unordered_map<std::string, std::optional<std::filesystem::path>> resolved_texture_path_cache;

        const AssetRef default_blinn_material = db.GetNewAssetRef(
            AssetPath(db, std::filesystem::path("~/materials/solid_color_dark_grey_blinn_phong.asset"))
        );
        const AssetRef default_pbr_albedo =
            db.GetNewAssetRef(AssetPath(db, std::filesystem::path("~/textures/dark_grey.asset")));
        const AssetRef default_pbr_mrao =
            db.GetNewAssetRef(AssetPath(db, std::filesystem::path("~/textures/white.asset")));
        output.default_blinn_material = default_blinn_material;

        // Iterate material slots referenced by imported submeshes and build each material once.
        for (uint32_t material_index : submesh_material_indices) {
            if (output.material_refs.contains(static_cast<size_t>(material_index))) {
                continue;
            }
            if (material_index >= scene.mNumMaterials || scene.mMaterials[material_index] == nullptr) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Mesh references invalid material slot %u; falling back to builtin default material.",
                    material_index
                );
                output.material_refs[material_index] = default_blinn_material;
                continue;
            }

            const aiMaterial &source_material = *scene.mMaterials[material_index];
            auto *material_asset = am.CreateAsset<MaterialAsset>();

            aiString source_name;
            source_material.Get(AI_MATKEY_NAME, source_name);
            const std::string fallback_material_name = model_name + "_material_" + std::to_string(material_index);
            const std::string base_material_name =
                source_name.length > 0 ? source_name.C_Str() : fallback_material_name;
            material_asset->m_name = import_shared::MakeUniqueAssetName(base_material_name, name_counters);

            // Lambda: create a SolidColorTexture fallback asset for missing texture channels.
            auto create_solid_color_texture = [&](const aiColor4D &color, const char *suffix) -> AssetRef {
                auto *solid_color = am.CreateAsset<SolidColorTextureAsset>();
                solid_color->m_name =
                    import_shared::MakeUniqueAssetName(material_asset->m_name + suffix, name_counters);
                solid_color->m_color = ToGlm(color);
                output.created_texture_assets.push_back(solid_color);
                return AssetRef(solid_color->GetGUID());
            };

            // Lambda: resolve, cache and build external texture referenced by one Assimp texture semantic.
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

                const std::string raw_texture_key = texture_path.C_Str();
                std::optional<std::filesystem::path> resolved_texture_path;
                auto cache_it = resolved_texture_path_cache.find(raw_texture_key);
                if (cache_it != resolved_texture_path_cache.end()) {
                    resolved_texture_path = cache_it->second;
                } else {
                    resolved_texture_path = ResolveTexturePath(path, texture_path);
                    resolved_texture_path_cache.emplace(raw_texture_key, resolved_texture_path);
                }
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

                auto *image_texture = am.CreateAsset<Image2DTextureAsset>();
                image_texture->LoadFromFile(*resolved_texture_path);
                image_texture->m_name = import_shared::MakeUniqueAssetName(
                    model_name + "_" + resolved_texture_path->stem().string(), name_counters
                );
                output.created_texture_assets.push_back(image_texture);

                const AssetRef texture_ref(image_texture);
                texture_refs_by_path.emplace(resolved_key, texture_ref);
                out_prop = MaterialProperty(texture_ref, MaterialProperty::Type::Texture);
                return true;
            };

            const bool use_pbr = ShouldUsePBR(source_material);
            if (use_pbr) {
                // PBR branch: build PBR material properties and PBR texture channels.
                material_asset->m_library =
                    db.GetNewAssetRef(AssetPath(db, std::filesystem::path("~/material_libraries/PBRLibrary.asset")));

                aiColor4D base_color(0.2f, 0.2f, 0.2f, 1.0f);
                source_material.Get(AI_MATKEY_BASE_COLOR, base_color);

                // Blinn-Phong branch: build legacy shading properties and diffuse texture path.
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
                material_asset->m_library = db.GetNewAssetRef(
                    AssetPath(db, std::filesystem::path("~/material_libraries/BlinnPhongLibrary.asset"))
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

            output.material_refs[material_index] = AssetRef(material_asset->GetGUID());
            output.created_material_assets.push_back(material_asset);
        }

        return output;
    }
} // namespace Engine::detail
