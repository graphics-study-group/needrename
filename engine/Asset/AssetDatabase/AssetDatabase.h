#ifndef ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED
#define ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED

#include "Asset/asset_export.h"
#include "AssetPath.h"
#include <Core/guid.h>
#include <optional>

namespace AnnoRefl {
    class Archive;
} // namespace AnnoRefl

namespace Engine {
    class AssetRef;

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
        /// @return path to the asset
        virtual AssetPath GetAssetPath(GUID guid) const = 0;

        /// @brief Get an unloaded AssetRef for the asset at the given path.
        /// @param path the in-project path of the asset
        /// @return an unloaded AssetRef carrying the asset's GUID
        /// @throw std::runtime_error when the path is not registered
        virtual AssetRef GetNewAssetRef(const AssetPath &path) const = 0;

        /// @brief Get the GUID registered for the given path.
        /// @param path the in-project path of the asset
        /// @return the asset's GUID, or std::nullopt when the path is not registered
        virtual std::optional<GUID> GetGUID(const AssetPath &path) const = 0;
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED
