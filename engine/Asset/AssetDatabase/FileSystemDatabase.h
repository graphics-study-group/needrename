#ifndef ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
#define ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED

#include "AssetDatabase.h"
#include <Core/guid.h>

namespace Engine {
    class FileSystemDatabase : public AssetDatabase {
    public:
        static constexpr const char *k_asset_file_extension = ".asset";

        FileSystemDatabase() = default;
        virtual ~FileSystemDatabase() = default;

        struct IterImpl : public AssetDatabase::ListRange::IterImplBase {
            std::filesystem::path m_root;
            std::string m_prefix;
            bool m_recursive;
            std::unique_ptr<std::filesystem::directory_iterator> m_it{};
            std::unique_ptr<std::filesystem::recursive_directory_iterator> m_rit{};

            IterImpl(std::filesystem::path root, const std::string &prefix, bool recursive);
            std::unique_ptr<IterImplBase> begin_clone() const override;
            bool next(AssetPair &out) override;
        };

        /// @brief Save the archive.
        virtual void SaveArchive(Serialization::Archive &archive, std::filesystem::path path) override;
        /// @brief Load the archive.
        virtual void LoadArchive(Serialization::Archive &archive, std::filesystem::path path) override;
        /**
         * @brief List all assets in the specified directory.
         *
         * @param directory The directory to search for assets.
         * @param recursive Whether to search recursively.
         * @return A lazy input-range of (project_path, GUID) pairs.
         */
        virtual ListRange ListAssets(std::filesystem::path directory = {}, bool recursive = true) override;

        std::filesystem::path GetAssetsDirectory() const;
        void SetBuiltinAssetPath(const std::filesystem::path &path);
        void SetProjectAssetPath(const std::filesystem::path &path);

    protected:
        /// @brief Path to the engine built-in assets
        std::filesystem::path m_builtin_asset_path{};
        /// @brief Path to the current project assets
        std::filesystem::path m_project_asset_path{};

        std::filesystem::path ProjectPathToFilesystemPath(const std::filesystem::path &project_path) const;
        std::filesystem::path FilesystemPathToProjectPath(const std::filesystem::path &fs_path) const;
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
