#ifndef ASSET_LOADER_FBXLOADER_INCLUDED
#define ASSET_LOADER_FBXLOADER_INCLUDED

#include "AssimpImportShared.h"

#include <filesystem>
#include <memory>

namespace Engine {
    class AssetManager;
    class FileSystemDatabase;

    class FbxLoader {
    public:
        FbxLoader();
        virtual ~FbxLoader() = default;

        /**
         * @brief Load an external FBX resource and serialize it into project assets.
         * @param path Path to the external FBX resource.
         * @param path_in_project Output directory relative to the project asset directory.
         * @param scale_factor Uniform scale applied to imported positions.
         * @param convert_coordinate_system Whether to convert from FBX Y-up/-Z-forward basis to engine basis.
         */
        void LoadFbxResource(
            const std::filesystem::path &path,
            const std::filesystem::path &path_in_project,
            float scale_factor = 0.01f,
            bool convert_coordinate_system = true
        );

        /**
         * @brief Load an FBX resource to runtime assets only (no asset file generated).
         * @param path Path to the external FBX resource.
         * @param scale_factor Uniform scale applied to imported positions.
         * @param convert_coordinate_system Whether to convert from FBX Y-up/-Z-forward basis to engine basis.
         * @return Import result that contains created runtime asset refs.
         */
        ImportResult LoadFbxInMemory(
            const std::filesystem::path &path, float scale_factor = 0.01f, bool convert_coordinate_system = true
        );

    private:
        std::weak_ptr<AssetManager> m_asset_manager{};
        std::weak_ptr<FileSystemDatabase> m_database{};
    };
} // namespace Engine

#endif // ASSET_LOADER_FBXLOADER_INCLUDED
