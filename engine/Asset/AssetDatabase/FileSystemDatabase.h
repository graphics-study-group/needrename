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

        /// @brief Save the archive.
        virtual void SaveArchive(Serialization::Archive &archive, std::filesystem::path path) override;
        /// @brief Load the archive.
        virtual void LoadArchive(Serialization::Archive &archive, std::filesystem::path path) override;
        /**
         * @brief List all assets in the specified directory.
         *
         * @param directory The directory to search for assets.
         * @param recursive Whether to search recursively.
         * @return std::vector<std::pair<std::filesystem::path, GUID>> A list of asset paths and their GUIDs.
         */
        virtual std::vector<std::pair<std::filesystem::path, GUID>> ListAssets(
            std::filesystem::path directory = {}, bool recursive = true
        ) override;

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
        /// @brief Get the GUID of an asset file. Return false if the file is not found or not an asset file.
        bool GetGUID(const std::filesystem::path &asset_path, GUID &out_guid) const;
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
