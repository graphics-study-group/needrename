#ifndef ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
#define ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED

#include "Asset/asset_export.h"
#include "AssetDatabase.h"
#include <Asset/AssetRef.h>
#include <Core/guid.h>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace Engine {
    /**
     * @brief An implementation of AssetDatabase that uses the file system to store assets.
     */
    class ASSET_CORE_API FileSystemDatabase : public AssetDatabase {
    public:
        static constexpr const char *k_asset_file_extension = ".asset";

        FileSystemDatabase() = default;
        virtual ~FileSystemDatabase() = default;

        struct AssetInfo {
            AssetPath path;
            GUID guid{};
            std::string type_name{};
            bool is_directory{};
        };

        /// @brief Add an asset guid to the system.
        /// @param guid the guid of the asset
        /// @param path the in-project path of the asset
        void AddAsset(const GUID &guid, const AssetPath &path);

        /// @brief Get the in-project path to the asset
        /// @param guid GUID of the asset
        /// @return path to the asset file
        AssetPath GetAssetPath(GUID guid) const override;

        /// @brief Get an unloaded AssetRef of the given path
        /// @param path the in-project path of the asset
        /// @return an unloaded AssetRef if the path exist, which only contains the GUID. throw std::runtime_error otherwise
        AssetRef GetNewAssetRef(const AssetPath &path) const;

        /// @brief Save the archive.
        virtual void SaveArchive(AnnoRefl::Archive &archive, GUID guid) override;
        /// @brief Load the archive.
        virtual void LoadArchive(AnnoRefl::Archive &archive, GUID guid) override;

        /// @brief Save the archive.
        void SaveArchive(AnnoRefl::Archive &archive, const AssetPath &path);
        /// @brief Load the archive.
        void LoadArchive(AnnoRefl::Archive &archive, const AssetPath &path);

        /**
         * @brief List the assets in a directory.
         *
         * @param path the target directory path
         * @param only_name whether to only list the names without loading asset info
         * @return std::vector<AssetInfo>
         */
        std::vector<AssetInfo> ListDirectory(const AssetPath &path) const;

        /// @brief Create a directory recursively under project assets.
        /// @param path Target directory path.
        /// @return True when the directory exists after this call.
        bool CreateDirectory(const AssetPath &path);

        /// @brief Move or rename an asset path inside project assets.
        /// @param from Source path to move.
        /// @param to Destination path.
        /// @return True if move succeeds.
        bool MovePath(const AssetPath &from, const AssetPath &to);

        /// @brief Delete an asset file or directory recursively from project assets.
        /// @param path Target path to remove.
        /// @return True if deletion succeeds.
        bool DeletePath(const AssetPath &path);

        const std::filesystem::path &GetProjectAssetsPath() const;
        const std::filesystem::path &GetBuiltinAssetsPath() const;
        void LoadBuiltinAssets(const std::filesystem::path &path);
        void LoadProjectAssets(const std::filesystem::path &path);

    protected:
        /// @brief Path to the engine built-in assets
        std::filesystem::path m_builtin_asset_path{};
        /// @brief Path to the current project assets
        std::filesystem::path m_project_asset_path{};

        std::unordered_map<GUID, AssetPath> m_assets_map{};
        std::unordered_map<AssetPath, GUID, AssetPath::Hash> m_path_to_guid{};
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
