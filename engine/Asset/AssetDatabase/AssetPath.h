#ifndef ASSET_ASSETDATABASE_ASSETPATH_INCLUDED
#define ASSET_ASSETDATABASE_ASSETPATH_INCLUDED

#include "Asset/asset_export.h"
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace Engine {
    /**
     * @brief A value type that addresses an asset by a named scheme plus a
     * relative path, stored as a normalized `scheme://path` string.
     *
     * The engine reserves the `res` (project assets), `builtin` (engine built-in
     * assets) and `usr` (user data) schemes. AssetPath is independent of any
     * database or backend; converting a path to an on-disk location is the
     * responsibility of the storage backend (see FileSystemDatabase).
     */
    class ASSET_CORE_API AssetPath {
    public:
        static constexpr const char *k_scheme_res = "res";
        static constexpr const char *k_scheme_builtin = "builtin";
        static constexpr const char *k_scheme_usr = "usr";

        AssetPath() = default;
        // Construct from a full "scheme://path" string. The scheme is normalized
        // to lowercase and the subpath is lexically normalized with root clamping.
        explicit AssetPath(std::string_view path);
        // Construct from a scheme and a subpath.
        AssetPath(std::string_view scheme, std::string_view subpath);

        bool operator==(const AssetPath &other) const = default;

        struct ASSET_CORE_API Hash {
            std::size_t operator()(const AssetPath &p) const;
        };

        /// @brief The scheme component (empty for an empty path).
        std::string_view GetScheme() const;
        /// @brief The subpath component as a generic filesystem path.
        std::filesystem::path GetSubPath() const;
        /// @brief True when the path holds no value.
        bool IsEmpty() const;
        /// @brief The parent path; the root path returns itself.
        AssetPath parent_path() const;
        /// @brief The final path component (empty at a scheme root).
        std::string filename() const;
        /// @brief Append a child path, re-normalizing the result.
        AssetPath operator/(std::string_view child) const;
        /// @brief The normalized `scheme://path` string.
        const std::string &ToString() const;
        /// @brief Alias of ToString(), kept for display and logging call sites.
        const std::string &generic_string() const;

    private:
        std::string m_value{};
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_ASSETPATH_INCLUDED
