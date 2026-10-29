#include <Core/FileSystem/DataRoot.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
    constexpr const char *k_data_root_env_var = "NEEDRENAME_DATA_ROOT";

    // The install layout the locator resolves against (see the install-layout
    // spec). Spelled out here so the test exercises the documented names rather
    // than the internal layout macros.
    constexpr const char *k_layout_subdirs[] = {"builtin_assets", "projects", "assets", "spv", "test_data"};

    int g_failures = 0;

    void Check(bool condition, const std::string &message) {
        if (condition) {
            return;
        }
        std::cerr << "FAILED: " << message << '\n';
        ++g_failures;
    }

    void CreateLayout(const std::filesystem::path &root) {
        for (const char *subdir : k_layout_subdirs) {
            std::error_code ec;
            std::filesystem::create_directories(root / subdir, ec);
        }
    }

    std::filesystem::path ScratchDirectory(const char *name) {
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "needrename_data_root_test" / name;
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
        return std::filesystem::weakly_canonical(root, ec);
    }

    std::filesystem::path ExecutableDirectory() {
        const char *base_path = SDL_GetBasePath();
        if (base_path == nullptr) {
            return {};
        }
        return std::filesystem::path(base_path);
    }
} // namespace

int main() {
    std::error_code ec;

    // --- Developer fallback: no override, no install layout next to the test --
    SDL_unsetenv_unsafe(k_data_root_env_var);
    Engine::ResetDataRootCache();
    {
        const Engine::DataRootResolution &resolution = Engine::ResolveDataRoot();
        Check(
            resolution.source == Engine::DataRootSource::DeveloperFallback,
            "no override resolves the developer fallback"
        );
        Check(std::filesystem::is_directory(Engine::BuiltinAssetsDir()), "developer built-in assets resolve");
        Check(std::filesystem::is_directory(Engine::ProjectsDir()), "developer projects resolve");
        Check(std::filesystem::is_directory(Engine::AssetsDir()), "developer sample assets resolve");
        Check(std::filesystem::is_directory(Engine::TestDataDir()), "developer test data resolves");
    }

    // --- Installed layout: `<exe dir>/../share/needrename` --------------------
    const std::filesystem::path install_root = ExecutableDirectory() / ".." / "share" / "needrename";
    CreateLayout(install_root);
    Engine::ResetDataRootCache();
    {
        const Engine::DataRootResolution &resolution = Engine::ResolveDataRoot();
        Check(resolution.source == Engine::DataRootSource::Installed, "install layout resolves before the fallback");
        Check(
            std::filesystem::weakly_canonical(Engine::DataRoot(), ec)
                == std::filesystem::weakly_canonical(install_root, ec),
            "installed data root is the executable-relative share/needrename"
        );
        Check(
            Engine::BuiltinAssetsDir() == Engine::DataRoot() / "builtin_assets",
            "installed built-in assets are root-relative"
        );
    }
    std::filesystem::remove_all(install_root, ec);

    // --- Environment override wins over everything ----------------------------
    const std::filesystem::path override_root = ScratchDirectory("override");
    CreateLayout(override_root);
    SDL_setenv_unsafe(k_data_root_env_var, override_root.string().c_str(), 1);
    Engine::ResetDataRootCache();
    {
        const Engine::DataRootResolution &resolution = Engine::ResolveDataRoot();
        Check(resolution.source == Engine::DataRootSource::Environment, "environment override wins");
        Check(
            std::filesystem::weakly_canonical(Engine::DataRoot(), ec) == override_root,
            "data root is the NEEDRENAME_DATA_ROOT directory"
        );
        Check(
            Engine::BuiltinAssetsDir() == Engine::DataRoot() / "builtin_assets",
            "override built-in assets are root-relative"
        );
    }

    // --- Failure: the override resolves but a required directory is missing ---
    const std::filesystem::path incomplete_root = ScratchDirectory("incomplete");
    SDL_setenv_unsafe(k_data_root_env_var, incomplete_root.string().c_str(), 1);
    Engine::ResetDataRootCache();
    {
        bool threw = false;
        try {
            static_cast<void>(Engine::BuiltinAssetsDir());
        } catch (const std::runtime_error &error) {
            threw = true;
            const std::string message = error.what();
            Check(
                message.find("builtin_assets") != std::string::npos,
                "missing-directory diagnostic names the directory that is missing"
            );
            Check(
                message.find(incomplete_root.string()) != std::string::npos,
                "missing-directory diagnostic names the resolved path"
            );
            Check(
                message.find(k_data_root_env_var) != std::string::npos,
                "missing-directory diagnostic names the resolution step"
            );
        }
        Check(threw, "missing required directory throws");
    }

    // Leave the process with the default resolution for any later work.
    SDL_unsetenv_unsafe(k_data_root_env_var);
    Engine::ResetDataRootCache();
    std::filesystem::remove_all(override_root, ec);
    std::filesystem::remove_all(incomplete_root, ec);
    std::filesystem::remove_all(override_root.parent_path(), ec);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "data root locator checks passed\n";
    return 0;
}
