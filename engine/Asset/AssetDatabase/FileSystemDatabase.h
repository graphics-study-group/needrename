#ifndef ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
#define ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED

#include "AssetDatabase.h"
#include <Core/guid.h>
#include <unordered_map>
#include <vector>

namespace Engine {
    class AssetRef;
    class FileSystemDatabase;

    /**
     * @brief A path class that represents an asset path in the project.
     */
    class AssetPath : private std::filesystem::path {
    private:
        const FileSystemDatabase &m_database;

    public:
        AssetPath(const FileSystemDatabase &db);
        // Construct from a in-project path directly. Will automatically lexically normalize it.
        AssetPath(const FileSystemDatabase &db, const std::filesystem::path &path);
        AssetPath(const AssetPath &other) = default;
        AssetPath &operator=(const AssetPath &other);
        bool operator==(const AssetPath &other) const;
        struct Hash {
            std::size_t operator()(const AssetPath &p) const;
        };

        std::filesystem::path to_absolute_path() const;
        // Construct from an absolute path.
        void from_absolute_path(const std::filesystem::path &absolute_path);
        AssetPath parent_path() const;

        using std::filesystem::path::iterator;
        using std::filesystem::path::begin;
        using std::filesystem::path::end;
        using std::filesystem::path::lexically_normal;
        using std::filesystem::path::empty;
        using std::filesystem::path::generic_string;
        using std::filesystem::path::filename;
    };

    /**
     * @brief An implementation of AssetDatabase that uses the file system to store assets.
     */
    class FileSystemDatabase : public AssetDatabase {
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
        AssetPath GetAssetPath(GUID guid) const;

        /// @brief Get an unloaded AssetRef of the given path
        /// @param path the in-project path of the asset
        /// @return an unloaded AssetRef if the path exist, which only contains the GUID. nullptr otherwise
        std::shared_ptr<AssetRef> GetNewAssetRef(const AssetPath &path) const;

        /// @brief Save the archive.
        virtual void SaveArchive(Serialization::Archive &archive, GUID guid) override;
        /// @brief Load the archive.
        virtual void LoadArchive(Serialization::Archive &archive, GUID guid) override;

        /// @brief Save the archive.
        void SaveArchive(Serialization::Archive &archive, const AssetPath &path);
        /// @brief Load the archive.
        void LoadArchive(Serialization::Archive &archive, const AssetPath &path);

        /**
         * @brief List the assets in a directory.
         *
         * @param path the target directory path
         * @param only_name whether to only list the names without loading asset info
         * @return std::vector<AssetInfo>
         */
        std::vector<AssetInfo> ListDirectory(const AssetPath &path) const;

        const std::filesystem::path &GetProjectAssetsPath() const;
        const std::filesystem::path &GetBuiltinAssetsPath() const;
        void LoadBuiltinAssets(const std::filesystem::path &path);
        void LoadProjectAssets(const std::filesystem::path &path);

    protected:
        /// @brief Path to the engine built-in assets
        std::filesystem::path m_builtin_asset_path{};
        /// @brief Path to the current project assets
        std::filesystem::path m_project_asset_path{};

        std::unordered_map<GUID, AssetPath, GUIDHash> m_assets_map{};
        std::unordered_map<AssetPath, GUID, AssetPath::Hash> m_path_to_guid{};
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_FILESYSTEMDATABASE_INCLUDED
