#ifndef ASSET_LOADER_FBXLOADER_INCLUDED
#define ASSET_LOADER_FBXLOADER_INCLUDED

#include <filesystem>
#include <memory>

namespace Engine {
    class AssetManager;
    class FileSystemDatabase;

    class FbxLoader {
    public:
        FbxLoader();
        virtual ~FbxLoader() = default;

        /// @brief Load an external FBX resource and serialize it into project assets.
        /// @param path Path to the external FBX resource.
        /// @param path_in_project Output directory relative to the project asset directory.
        void LoadFbxResource(const std::filesystem::path &path, const std::filesystem::path &path_in_project);

    private:
        std::weak_ptr<AssetManager> m_asset_manager{};
        std::weak_ptr<FileSystemDatabase> m_database{};
    };
} // namespace Engine

#endif // ASSET_LOADER_FBXLOADER_INCLUDED
