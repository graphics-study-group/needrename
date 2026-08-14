#ifndef ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED
#define ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED

#include "Asset/asset_export.h"
#include <Core/guid.h>
#include <filesystem>
#include <iterator>
#include <memory>

namespace AnnoRefl {
    class Archive;
} // namespace AnnoRefl

namespace Engine {
    class FileSystemDatabase;

    /**
     * @brief A path class that represents an asset path in the project.
     *
     * An asset path stores an internal unique file system path for an asset.
     * It has two special root directories:
     * - A path starting with `~/` refers to a built-in asset;
     * - A path starting with `/` refers to an asset in project, whose root directory
     *   corresponds to the project directory.
     *
     * Call `to_absolute_path()` to get an absolute path for this asset on the local disk.
     */
    class ASSET_CORE_API AssetPath : private std::filesystem::path {
    private:
        const FileSystemDatabase &m_database;

    public:
        AssetPath(const FileSystemDatabase &db);
        // Construct from a in-project path directly. Will automatically lexically normalize it.
        AssetPath(const FileSystemDatabase &db, const std::filesystem::path &path);
        AssetPath(const AssetPath &other) = default;
        AssetPath &operator=(const AssetPath &other);
        bool operator==(const AssetPath &other) const;
        struct ASSET_CORE_API Hash {
            std::size_t operator()(const AssetPath &p) const;
        };

        std::filesystem::path to_absolute_path() const;
        // Construct from an absolute path.
        void from_absolute_path(const std::filesystem::path &absolute_path);
        AssetPath parent_path() const;

        using std::filesystem::path::begin;
        using std::filesystem::path::empty;
        using std::filesystem::path::end;
        using std::filesystem::path::filename;
        using std::filesystem::path::generic_string;
        using std::filesystem::path::iterator;
        using std::filesystem::path::lexically_normal;
    };

    /**
     * @brief An interface defines a map between asset GUID and asset data storage.
     * This interface provides methods to save and load archives to and from the database.
     * Different implementations can use different storage backends, such as file system,
     * or packaged files.
     */
    class ASSET_CORE_API AssetDatabase {
    public:
        AssetDatabase() = default;
        virtual ~AssetDatabase() = default;

        /// @brief Save the archive.
        virtual void SaveArchive(AnnoRefl::Archive &archive, GUID guid) = 0;
        /// @brief Load the archive.
        virtual void LoadArchive(AnnoRefl::Archive &archive, GUID guid) = 0;

        /// @brief Get the in-project path to the asset identified by its GUID.
        /// @param guid GUID of the asset
        /// @return path to the asset file
        virtual AssetPath GetAssetPath(GUID guid) const = 0;
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED
