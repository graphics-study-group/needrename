#include "DataRoot.h"

#include <cmake_config.h>

#include <SDL3/SDL.h>

#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace Engine {
    namespace {
        // TODO: rename - the data-root override environment variable carries the
        // placeholder package name.
        constexpr const char *k_data_root_env_var = "NEEDRENAME_DATA_ROOT";
        constexpr const char *k_installed_data_subdir = "share/needrename";

        std::mutex g_resolve_mutex{};
        std::optional<DataRootResolution> g_resolution{};

        bool IsDirectory(const std::filesystem::path &path) {
            if (path.empty()) {
                return false;
            }
            std::error_code ec;
            return std::filesystem::is_directory(path, ec);
        }

        std::filesystem::path Absolute(const std::filesystem::path &path) {
            std::error_code ec;
            const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
            return (ec ? path : absolute).lexically_normal();
        }

        std::filesystem::path ExecutableDirectory() {
            const char *base_path = SDL_GetBasePath();
            if (base_path == nullptr) {
                return {};
            }
            return std::filesystem::path(base_path);
        }

        DataRootResolution ResolveUncached() {
            std::vector<std::string> attempts;

            const char *env_root = SDL_getenv(k_data_root_env_var);
            if (env_root != nullptr && env_root[0] != '\0') {
                const std::filesystem::path candidate = Absolute(env_root);
                attempts.push_back(std::string(k_data_root_env_var) + "=" + candidate.string());
                if (IsDirectory(candidate)) {
                    return DataRootResolution{
                        candidate,
                        DataRootSource::Environment,
                        std::string(k_data_root_env_var) + "=" + candidate.string(),
                    };
                }
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "Ignoring %s='%s': not a directory.",
                    k_data_root_env_var,
                    candidate.string().c_str()
                );
            }

            const std::filesystem::path executable_directory = ExecutableDirectory();
            if (!executable_directory.empty()) {
                const std::filesystem::path candidate =
                    (executable_directory / ".." / k_installed_data_subdir).lexically_normal();
                attempts.push_back(candidate.string());
                if (IsDirectory(candidate)) {
                    return DataRootResolution{
                        candidate,
                        DataRootSource::Installed,
                        "executable-relative " + std::string(k_installed_data_subdir),
                    };
                }
            }

            const std::filesystem::path developer_root(ENGINE_DEV_DATA_ROOT_DIR);
            attempts.push_back(developer_root.string());
            if (IsDirectory(developer_root)) {
                return DataRootResolution{
                    developer_root,
                    DataRootSource::DeveloperFallback,
                    "compile-time developer fallback",
                };
            }

            std::string message = "Cannot resolve the runtime data root. Tried:";
            for (const std::string &attempt : attempts) {
                message += "\n  " + attempt;
            }
            message += "\nSet " + std::string(k_data_root_env_var) + " to an existing data root directory.";
            throw std::runtime_error(message);
        }

        const DataRootResolution &CachedResolution() {
            std::scoped_lock lock(g_resolve_mutex);
            if (!g_resolution.has_value()) {
                g_resolution = ResolveUncached();
            }
            return *g_resolution;
        }
    } // namespace

    const DataRootResolution &ResolveDataRoot() {
        return CachedResolution();
    }

    const std::filesystem::path &DataRoot() {
        return CachedResolution().root;
    }

    std::filesystem::path ResolveDataDir(std::string_view subdir, const std::filesystem::path &developer_fallback) {
        const DataRootResolution &resolution = CachedResolution();
        const std::filesystem::path candidate =
            resolution.source == DataRootSource::DeveloperFallback
                ? developer_fallback.lexically_normal()
                : (resolution.root / std::filesystem::path(subdir)).lexically_normal();

        if (!IsDirectory(candidate)) {
            throw std::runtime_error(
                "Runtime data directory '" + std::string(subdir) + "' is missing at " + candidate.string()
                + " (data root resolved via " + resolution.step + "). Set " + std::string(k_data_root_env_var)
                + " to a complete data root directory."
            );
        }
        return candidate;
    }

    std::filesystem::path BuiltinAssetsDir() {
        return ResolveDataDir(ENGINE_DATA_SUBDIR_BUILTIN_ASSETS, ENGINE_DEV_BUILTIN_ASSETS_DIR);
    }

    std::filesystem::path ProjectsDir() {
        return ResolveDataDir(ENGINE_DATA_SUBDIR_PROJECTS, ENGINE_DEV_PROJECTS_DIR);
    }

    std::filesystem::path AssetsDir() {
        return ResolveDataDir(ENGINE_DATA_SUBDIR_ASSETS, ENGINE_DEV_ASSETS_DIR);
    }

    std::filesystem::path TestDataDir() {
        return ResolveDataDir(ENGINE_DATA_SUBDIR_TEST_DATA, ENGINE_DEV_TESTS_DIR);
    }

    void ResetDataRootCache() {
        std::scoped_lock lock(g_resolve_mutex);
        g_resolution.reset();
    }
} // namespace Engine
