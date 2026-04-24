#ifndef ASSET_LOADER_ASSIMPLOADER_INCLUDED
#define ASSET_LOADER_ASSIMPLOADER_INCLUDED

#include <filesystem>
#include <memory>

namespace Engine {
    class AssetManager;
    class FileSystemDatabase;

    class AssimpLoader {
    public:
        AssimpLoader();
        virtual ~AssimpLoader() = default;

        /// @brief Load an external model resource (OBJ/FBX) and serialize it into project assets.
        /// @param path Path to the external model resource.
        /// @param path_in_project Output directory relative to the project asset directory.
        void LoadResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project);

    private:
        std::weak_ptr<AssetManager> m_asset_manager{};
        std::weak_ptr<FileSystemDatabase> m_database{};
    };
} // namespace Engine

#endif // ASSET_LOADER_ASSIMPLOADER_INCLUDED
