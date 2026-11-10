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
    };

    /**
     * @brief A resolved runtime data root and the step that resolved it.
     */
    struct DataRootResolution {
        std::filesystem::path root{};
        DataRootSource source{DataRootSource::Installed};
        std::string step{};
    };

    /**
     * @brief Resolve the runtime data root, caching the result.
     *
     * The root is the first match of `NEEDRENAME_DATA_ROOT` (when it names an
     * existing directory) and `<executable dir>/../share/needrename`.
     *
     * There is no compile-time path into the source tree, on purpose. A binary
     * carries no knowledge of where it was built, so it cannot silently read a
     * checkout that does not match its configuration: the build tree is laid out as
     * a prefix - programs in `bin/`, data in `share/needrename/` - so one rule
     * resolves both it and an installation. A run that wants a different tree - the
     * checkout, for live asset editing - says so with `NEEDRENAME_DATA_ROOT`.
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
     * Every category lives in a subdirectory of the root, in a build tree and in
     * an install alike.
     *
     * @param subdir Layout subdirectory name beneath the data root.
     * @return An existing directory.
     * @exception std::runtime_error The directory does not exist; the message
     * names the path attempted and the resolution step.
     */
    CORE_API std::filesystem::path ResolveDataDir(std::string_view subdir);

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
