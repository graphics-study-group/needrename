#ifndef ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
#define ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED

#include "AssetDatabase.h"
#include <Core/guid.h>
#include <unordered_map>
#include <vector>

namespace Engine {
    class AssetRef;

    class FileSystemDatabase : public AssetDatabase {
    public:
        static constexpr const char *k_asset_file_extension = ".asset";

        FileSystemDatabase() = default;
        virtual ~FileSystemDatabase() = default;

        struct AssetInfo {
            std::filesystem::path path{};
            GUID guid{};
            std::string type_name{};
            bool is_directory{};
        };

        /// @brief Add an asset guid to the system.
        /// @param guid the guid of the asset
        /// @param path the in-project path of the asset
        void AddAsset(const GUID &guid, const std::filesystem::path &path);

        /// @brief Get the in-project path to the asset
        /// @param guid GUID of the asset
        /// @return path to the asset file
        std::filesystem::path GetAssetPath(GUID guid) const;

        /// @brief Get an unloaded AssetRef of the given path
        /// @param path the in-project path of the asset
        /// @return an unloaded AssetRef if the path exist, which only contains the GUID. nullptr otherwise
        std::shared_ptr<AssetRef> GetNewAssetRef(const std::filesystem::path &path) const;

        /// @brief Save the archive.
        virtual void SaveArchive(Serialization::Archive &archive, GUID guid) override;
        /// @brief Load the archive.
        virtual void LoadArchive(Serialization::Archive &archive, GUID guid) override;

        /// @brief Save the archive.
        void SaveArchive(Serialization::Archive &archive, const std::filesystem::path &path);
        /// @brief Load the archive.
        void LoadArchive(Serialization::Archive &archive, const std::filesystem::path &path);

        /**
         * @brief List the assets in a directory.
         * 
         * @param path the target directory path
         * @param only_name whether to only list the names without loading asset info
         * @return std::vector<AssetInfo> 
         */
        std::vector<AssetInfo> ListDirectory(const std::filesystem::path &path) const;

        std::filesystem::path GetAssetsDirectory() const;
        void LoadBuiltinAssets(const std::filesystem::path &path);
        void LoadProjectAssets(const std::filesystem::path &path);

    protected:
        /// @brief Path to the engine built-in assets
        std::filesystem::path m_builtin_asset_path{};
        /// @brief Path to the current project assets
        std::filesystem::path m_project_asset_path{};

        std::unordered_map<GUID, std::string, GUIDHash> m_assets_map{};
        std::unordered_map<std::string, GUID> m_path_to_guid{};

        std::filesystem::path ProjectPathToFilesystemPath(const std::filesystem::path &project_path) const;
        std::filesystem::path FilesystemPathToProjectPath(const std::filesystem::path &fs_path) const;
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
