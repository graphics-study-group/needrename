#ifndef ASSET_LOADER_GLTFLOADER_INCLUDED
#define ASSET_LOADER_GLTFLOADER_INCLUDED

#include "AssimpImportShared.h"

#include <filesystem>
#include <memory>

namespace Engine {
    class AssetManager;
    class FileSystemDatabase;

    class GltfLoader {
    public:
        GltfLoader();
        virtual ~GltfLoader() = default;

        /**
         * @brief Load an external glTF resource and serialize it into project assets.
         * @param path Path to the external glTF/GLB resource.
         * @param path_in_project Output directory relative to the project asset directory.
         */
        void LoadGltfResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project);

        /**
         * @brief Load a glTF resource to runtime assets only (no asset file generated).
         * @param path Path to the external glTF/GLB resource.
         * @return Import result that contains created runtime asset refs.
         */
        ImportResult LoadGltfInMemory(const std::filesystem::path &path);

    private:
        std::weak_ptr<AssetManager> m_asset_manager{};
        std::weak_ptr<FileSystemDatabase> m_database{};
    };
} // namespace Engine

#endif // ASSET_LOADER_GLTFLOADER_INCLUDED
