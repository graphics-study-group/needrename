#ifndef ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED
#define ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED

#include <Core/guid.h>
#include <filesystem>
#include <iterator>
#include <memory>

namespace AnnoRefl {
    class Archive;
} // namespace AnnoRefl

namespace Engine {
    /**
     * @brief An interface defines a map between asset GUID and asset data storage.
     * This interface provides methods to save and load archives to and from the database.
     * Different implementations can use different storage backends, such as file system,
     * or packaged files.
     */
    class AssetDatabase {
    public:
        AssetDatabase() = default;
        virtual ~AssetDatabase() = default;

        /// @brief Save the archive.
        virtual void SaveArchive(AnnoRefl::Archive &archive, GUID guid) = 0;
        /// @brief Load the archive.
        virtual void LoadArchive(AnnoRefl::Archive &archive, GUID guid) = 0;
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED
