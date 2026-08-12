#ifndef ASSET_LOADER_OBJLOADER_INCLUDED
#define ASSET_LOADER_OBJLOADER_INCLUDED

#include "ImportTypes.h"

#include <filesystem>
#include <memory>

namespace Engine {
    class AssetManager;
    class FileSystemDatabase;

    class ObjLoader {
    public:
        ObjLoader();
        virtual ~ObjLoader() = default;

    public:
        /**
         * @brief Load an external obj resource, copy to the project asset directory, and create a meta file.
         * @param path Path to the external obj resource.
         * @param path_in_project Path to the output asset directory relative to the project asset directory.
         */
        void LoadObjResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project);

        /**
         * @brief Load an obj resource to runtime assets only (no asset file generated).
         * @param path Path to the external obj resource.
         * @return Import result that contains created runtime asset refs.
         */
        ImportResult LoadObjInMemory(const std::filesystem::path &path);

    private:
        std::weak_ptr<AssetManager> m_asset_manager{};
        std::weak_ptr<FileSystemDatabase> m_database{};
    };
} // namespace Engine

#endif // ASSET_LOADER_OBJLOADER_INCLUDED
