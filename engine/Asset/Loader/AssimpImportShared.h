#ifndef ASSET_LOADER_ASSIMPIMPORTSHARED_INCLUDED
#define ASSET_LOADER_ASSIMPIMPORTSHARED_INCLUDED

#include <Asset/AssetRef.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace Engine {
    class AssetManager;
    class FileSystemDatabase;

    /**
     * @brief Runtime output bundle produced by one model import operation.
     */
    struct ImportResult {
        /// Primary mesh asset generated from imported model geometry.
        AssetRef mesh_asset{};

        /// Material refs aligned with mesh submesh order, used for direct component binding.
        std::vector<AssetRef> mesh_material_assets{};

        /// Newly created material assets during this import (deduplicated by source material index).
        std::vector<AssetRef> created_material_assets{};

        /// Newly created texture assets during this import (including fallback generated textures).
        std::vector<AssetRef> created_texture_assets{};

        /// Optional scene asset that contains mesh object and imported light objects when enabled.
        std::optional<AssetRef> scene_asset{};

        /// Number of successfully imported lights written into the generated scene payload.
        uint32_t imported_light_count{0};
    };

    namespace detail {
        struct AssimpImportOptions {
            const char *source_name{"Model"};
            bool persist_assets{true};
            bool create_scene_asset{true};
            float scale_factor{1.0f};
            bool convert_coordinate_system{false};
        };

        /**
         * @brief Import model data through Assimp and build runtime asset references.
         *
         * This function supports both persisted mode (write generated assets into project storage)
         * and in-memory mode (return transient runtime assets only).
         *
         * @param path Path to the external model file.
         * @param path_in_project Optional output directory relative to project asset root.
         * @param asset_manager Asset manager used to create runtime assets.
         * @param database Asset database used when persisted import is enabled.
         * @param options Import behaviors such as source name and persistence switches.
         * @return ImportResult Summary of generated mesh/material/texture/scene asset references.
         */
        ImportResult ImportWithAssimp(
            const std::filesystem::path &path,
            const std::optional<std::filesystem::path> &path_in_project,
            const std::weak_ptr<AssetManager> &asset_manager,
            const std::weak_ptr<FileSystemDatabase> &database,
            const AssimpImportOptions &options
        );

        /**
         * @brief Import and persist model assets into project storage.
         *
         * This is a compatibility wrapper around ImportWithAssimp that always enables
         * persisted import and scene asset generation.
         *
         * @param path Path to the external model file.
         * @param path_in_project Output directory relative to project asset root.
         * @param asset_manager Asset manager used to create runtime assets.
         * @param database Asset database used to save generated assets.
         * @param options Import behaviors such as source name.
         */
        void LoadResourceWithAssimp(
            const std::filesystem::path &path,
            const std::filesystem::path &path_in_project,
            const std::weak_ptr<AssetManager> &asset_manager,
            const std::weak_ptr<FileSystemDatabase> &database,
            const AssimpImportOptions &options
        );
    } // namespace detail
} // namespace Engine

#endif // ASSET_LOADER_ASSIMPIMPORTSHARED_INCLUDED
