#ifndef ENGINE_CORE_FILESYSTEM_DATAROOT_INCLUDED
#define ENGINE_CORE_FILESYSTEM_DATAROOT_INCLUDED

#include "../core_export.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Engine {
    /**
     * @brief The resolution step that produced the runtime data root.
     */
    enum class DataRootSource {
        Environment,
        Installed,
        DeveloperFallback,
    };

    /**
     * @brief A resolved runtime data root and the step that resolved it.
     */
    struct DataRootResolution {
        std::filesystem::path root{};
        DataRootSource source{DataRootSource::DeveloperFallback};
        std::string step{};
    };

    /**
     * @brief Resolve the runtime data root, caching the result.
     *
     * The root is the first match of `NEEDRENAME_DATA_ROOT` (when it names an
     * existing directory), `../share/needrename` beneath the executable's
     * directory, and the compile-time developer fallback.
     *
     * @return The cached resolution, including the step that produced it.
     * @exception std::runtime_error No candidate directory exists.
     */
    CORE_API const DataRootResolution &ResolveDataRoot();

    /**
     * @brief Get the resolved runtime data root directory.
     *
     * @return The resolved data root.
     * @exception std::runtime_error No candidate directory exists.
     */
    CORE_API const std::filesystem::path &DataRoot();

    /**
     * @brief Resolve a data directory beneath the runtime data root.
     *
     * An installed data root (or an explicit `NEEDRENAME_DATA_ROOT`) stores
     * every category in a subdirectory of the root. The developer build tree
     * keeps its categories apart, so it names each one with its own
     * compile-time fallback.
     *
     * @param subdir Layout subdirectory name beneath the data root.
     * @param developer_fallback Directory to use when the root resolved through
     * the developer fallback.
     * @return An existing directory.
     * @exception std::runtime_error The directory does not exist; the message
     * names the path attempted and the resolution step.
     */
    CORE_API std::filesystem::path ResolveDataDir(
        std::string_view subdir, const std::filesystem::path &developer_fallback
    );

    /**
     * @brief Get the built-in asset database root (`builtin_assets/`).
     *
     * @return The directory holding the built-in assets.
     * @exception std::runtime_error The directory does not exist.
     */
    CORE_API std::filesystem::path BuiltinAssetsDir();

    /**
     * @brief Get the project template root (`projects/`).
     *
     * @return The directory holding the project templates.
     * @exception std::runtime_error The directory does not exist.
     */
    CORE_API std::filesystem::path ProjectsDir();

    /**
     * @brief Get the sample content root (`assets/`).
     *
     * @return The directory holding sample meshes, skyboxes and similar content.
     * @exception std::runtime_error The directory does not exist.
     */
    CORE_API std::filesystem::path AssetsDir();

    /**
     * @brief Get the test data root (`test_data/`).
     *
     * @return The directory holding test fixture files.
     * @exception std::runtime_error The directory does not exist.
     */
    CORE_API std::filesystem::path TestDataDir();

    /**
     * @brief Drop the cached resolution so the next access re-resolves it.
     *
     * @note Intended for tests that change `NEEDRENAME_DATA_ROOT` in-process.
     */
    CORE_API void ResetDataRootCache();
} // namespace Engine

#endif // ENGINE_CORE_FILESYSTEM_DATAROOT_INCLUDED
