#ifndef ASSET_LOADER_IMPORTSHAREDUTIL_INCLUDED
#define ASSET_LOADER_IMPORTSHAREDUTIL_INCLUDED

#include <Asset/Mesh/MeshAsset.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {
    class Asset;
    class AssetPath;
    class FileSystemDatabase;

    namespace detail::import_shared {
        /**
         * @brief Normalize a candidate asset name to a filesystem-safe identifier.
         *
         * Illegal filename characters and control characters are replaced with '_'.
         * Empty input is converted to "unnamed".
         *
         * @param name Raw input name from importer side.
         * @return Sanitized name that is safe for asset persistence.
         */
        std::string SanitizeAssetName(std::string name);

        /**
         * @brief Generate a unique asset name within one import session.
         *
         * The function first sanitizes the base name, then appends a numeric suffix
         * when the same name appears repeatedly.
         *
         * @param base_name Preferred source name.
         * @param name_counters Name-frequency map maintained by caller.
         * @return Unique name to be used for runtime/persisted assets.
         */
        std::string MakeUniqueAssetName(
            const std::string &base_name, std::unordered_map<std::string, uint32_t> &name_counters
        );

        /**
         * @brief Append one float attribute stream into packed submesh buffer.
         *
         * Bytes are appended to `buffer`, and the written byte range is saved into
         * `attr` as offset/size metadata.
         *
         * @param buffer Destination packed byte buffer.
         * @param data Source float array.
         * @param float_count Number of float values in `data`.
         * @param attr Output attribute descriptor that receives offset/size.
         */
        void AppendVertexAttribute(
            std::vector<std::byte> &buffer, const float *data, size_t float_count, MeshAsset::Submesh::Attributes &attr
        );

        /**
         * @brief Build a project-relative asset path with ".asset" suffix.
         *
         * @param database Database object used by AssetPath wrapper.
         * @param path_in_project Target folder relative to project asset root.
         * @param asset_name Logical asset name without extension.
         * @return Resolved asset path object.
         */
        AssetPath MakeAssetPath(
            FileSystemDatabase &database, const std::filesystem::path &path_in_project, const std::string &asset_name
        );

        /**
         * @brief Serialize and persist one runtime asset into database archive.
         *
         * @param database Target asset database.
         * @param asset Runtime asset instance to save.
         * @param path_in_project Target folder relative to project asset root.
         * @param asset_name Target asset file name without extension.
         */
        void SaveAsset(
            FileSystemDatabase &database,
            const Asset &asset,
            const std::filesystem::path &path_in_project,
            const std::string &asset_name
        );
    } // namespace detail::import_shared
} // namespace Engine

#endif // ASSET_LOADER_IMPORTSHAREDUTIL_INCLUDED
