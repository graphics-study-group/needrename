#ifndef ASSET_LOADER_ASSIMPIMPORTSHARED_INCLUDED
#define ASSET_LOADER_ASSIMPIMPORTSHARED_INCLUDED

#include <filesystem>
#include <memory>

namespace Engine {
    class AssetManager;
    class FileSystemDatabase;

    namespace detail {
        struct AssimpImportOptions {
            bool enable_fbx_compat{false};
            const char *source_name{"Model"};
        };

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
