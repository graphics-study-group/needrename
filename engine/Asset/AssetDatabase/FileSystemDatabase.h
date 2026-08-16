#ifndef ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
#define ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED

#include "Asset/asset_export.h"
#include "AssetDatabase.h"
#include <Asset/AssetRef.h>
#include <Core/guid.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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

        /// @brief Register a scheme mount, mapping the scheme to a disk root.
        /// @param scheme the scheme name (e.g. AssetPath::k_scheme_res)
        /// @param root the on-disk directory the scheme resolves to
        /// @param writable whether write operations are allowed for this mount
        void RegisterScheme(std::string_view scheme, const std::filesystem::path &root, bool writable = true);

        /// @brief Resolve an asset path to an absolute on-disk path.
        /// @throw std::runtime_error when the scheme is not mounted
        std::filesystem::path ToAbsolutePath(const AssetPath &path) const;

        /// @brief Map an absolute on-disk path back to the asset path of the deepest matching mount.
        /// @throw std::runtime_error when no mount contains the path
        AssetPath FromAbsolutePath(const std::filesystem::path &absolute_path) const;

        /// @brief Add an asset guid to the system.
        /// @param guid the guid of the asset
        /// @param path the in-project path of the asset
        void AddAsset(const GUID &guid, const AssetPath &path);

        /// @brief Get the in-project path to the asset
        /// @param guid GUID of the asset
        /// @return path to the asset
        AssetPath GetAssetPath(GUID guid) const override;

        /// @brief Get an unloaded AssetRef of the given path.
        AssetRef GetNewAssetRef(const AssetPath &path) const override;

        /// @brief Get the GUID registered for the given path.
        std::optional<GUID> GetGUID(const AssetPath &path) const override;

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

        /// @brief The on-disk root of the project assets mount (empty when not mounted).
        std::filesystem::path GetProjectAssetsPath() const;

        /// @brief Register the builtin mount and scan it for asset files.
        void LoadBuiltinAssets(const std::filesystem::path &path);

        /// @brief Register the project mount and scan it for asset files.
        void LoadProjectAssets(const std::filesystem::path &path);

    protected:
        struct Mount {
            std::filesystem::path root{};
            bool writable{true};
        };

        /// @brief Whether the path's scheme is mounted and writable.
        bool IsWritableScheme(const AssetPath &path) const;

        std::unordered_map<std::string, Mount> m_mounts{};
        std::unordered_map<GUID, AssetPath> m_assets_map{};
        std::unordered_map<AssetPath, GUID, AssetPath::Hash> m_path_to_guid{};
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
