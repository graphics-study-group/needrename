#ifndef ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED
#define ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED

#include <Core/guid.h>
#include <filesystem>

namespace Engine {
    namespace Serialization {
        class Archive;
    }

    /**
     * @brief An interface defines a map between asset paths and their data.
     * @details This interface provides methods to save and load archives to and from the database.
     * Different implementations can use different storage backends, such as file system,
     * or packaged files.
     */
    class AssetDatabase {
    public:
        AssetDatabase() = default;
        virtual ~AssetDatabase() = default;

        /// @brief Save the archive.
        virtual void SaveArchive(Serialization::Archive &archive, std::filesystem::path path) = 0;
        /// @brief Load the archive.
        virtual void LoadArchive(Serialization::Archive &archive, std::filesystem::path path) = 0;
        /**
         * @brief List all assets in the specified directory.
         *
         * @param directory The directory to search for assets.
         * @param recursive Whether to search recursively.
         * @return std::vector<std::pair<std::filesystem::path, GUID>> A list of asset paths and their GUIDs.
         */
        virtual std::vector<std::pair<std::filesystem::path, GUID>> ListAssets( // TODO: return iterator instead of vector
            std::filesystem::path directory = {}, bool recursive = true
        ) = 0;
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED
