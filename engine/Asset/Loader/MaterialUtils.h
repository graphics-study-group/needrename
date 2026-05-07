#ifndef ASSET_LOADER_MATERIALUTILS_INCLUDED
#define ASSET_LOADER_MATERIALUTILS_INCLUDED

#include <Asset/AssetRef.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fastgltf {
    struct Asset;
}

namespace Engine {
    class AssetManager;
    class FileSystemDatabase;
    class MaterialAsset;
    class TextureAsset;

    namespace detail {
        /**
         * @brief Aggregated outputs produced by one material-build pass.
         */
        struct MaterialBuildOutput {
            /// Default PBR material ref used by glTF path and PBR fallbacks.
            AssetRef default_pbr_material{};
            /// Source material index to runtime material ref mapping.
            std::unordered_map<size_t, AssetRef> material_refs{};
            /// Material assets created during this build pass.
            std::vector<MaterialAsset *> created_material_assets{};
            /// Texture assets created during this build pass.
            std::vector<TextureAsset *> created_texture_assets{};
        };

        /**
         * @brief Build runtime material/texture assets from glTF material table.
         *
         * The function imports current supported PBR channels, reports unsupported
         * extensions/maps with warnings, and returns per-material refs.
         *
         * @param asset Parsed fastgltf asset object.
         * @param path Source glTF path for resolving external textures.
         * @param am Asset manager used to create runtime assets.
         * @param db Asset database used to fetch builtin default refs.
         * @param model_name Base model name used for generated asset naming.
         * @param required_material_indices Material indices actually referenced by imported meshes.
         * @param name_counters Name deduplication map shared by importer pipeline.
         * @return Aggregated material build outputs and created asset pointers.
         */
        MaterialBuildOutput BuildMaterialsFromGltf(
            const fastgltf::Asset &asset,
            const std::filesystem::path &path,
            AssetManager &am,
            FileSystemDatabase &db,
            const std::string &model_name,
            const std::unordered_set<size_t> &required_material_indices,
            std::unordered_map<std::string, uint32_t> &name_counters
        );
    } // namespace detail
} // namespace Engine

#endif // ASSET_LOADER_MATERIALUTILS_INCLUDED
