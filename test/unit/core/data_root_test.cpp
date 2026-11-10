// data_root_test.cpp — the runtime data locator, checked read-only.
//
// The test runs twice (see test/unit/core/CMakeLists.txt): from the assembled
// product, where the data root is `<prefix>/share/needrename` one level above the
// executables, and from the build tree's run directory, where this build stages
// `<run dir>/share/needrename`. Both are found by proximity, so the test expects
// whichever directory exists beside its own and asserts that resolution, the
// categories beneath it, and that the explicit override still wins - without ever
// creating or deleting a directory. A test is not allowed to build a fake layout
// where it runs, because that is the real product's data root and a stray
// `remove_all` there would delete the build.

#include <Core/FileSystem/DataRoot.h>

#include <SDL3/SDL.h>

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <system_error>

namespace {
    constexpr const char *k_data_root_env_var = "NEEDRENAME_DATA_ROOT";

    int g_failures = 0;

    void Check(bool condition, const std::string &message) {
        if (condition) {
            return;
        }
        std::cerr << "FAILED: " << message << '\n';
        ++g_failures;
    }

    std::filesystem::path ExecutableDirectory() {
        const char *base_path = SDL_GetBasePath();
        if (base_path == nullptr) {
            return {};
        }
        return std::filesystem::path(base_path);
    }

    std::filesystem::path Canonical(const std::filesystem::path &path) {
        std::error_code ec;
        return std::filesystem::weakly_canonical(path, ec);
    }
} // namespace

int main() {
    // --- Proximity: the data root this build produced, with no override ------
    SDL_unsetenv_unsafe(k_data_root_env_var);
    Engine::ResetDataRootCache();

    // Which of the two layouts is this: the build tree stages the data root
    // inside the run directory, a product keeps it one level above the binaries.
    const std::filesystem::path run_directory_root = ExecutableDirectory() / "share" / "needrename";
    const bool from_build_tree = std::filesystem::is_directory(run_directory_root);
    const std::filesystem::path expected_root =
        from_build_tree ? run_directory_root : ExecutableDirectory() / ".." / "share" / "needrename";
    const Engine::DataRootSource expected_source =
        from_build_tree ? Engine::DataRootSource::BuildTree : Engine::DataRootSource::Installed;

    {
        const Engine::DataRootResolution &resolution = Engine::ResolveDataRoot();
        Check(
            resolution.source == expected_source,
            from_build_tree
                ? "the build tree's data root resolves beside the executable"
                : "the product's data root resolves from the executable's own directory"
        );
        Check(
            Canonical(Engine::DataRoot()) == Canonical(expected_root),
            from_build_tree ? "data root is <executable dir>/share/needrename"
                            : "data root is <executable dir>/../share/needrename"
        );
    }

    // Every category the runtime asks for exists beneath that root and is
    // root-relative. This is also the check that the build's staging and the
    // install rules agree about the layout.
    const std::set<std::string> categories{"builtin_assets", "assets", "projects", "spirv", "test_data"};
    for (const std::string &category : categories) {
        bool ok = false;
        try {
            ok = std::filesystem::is_directory(Engine::DataRoot() / category);
        } catch (const std::exception &error) {
            std::cerr << "resolving " << category << " threw: " << error.what() << '\n';
        }
        Check(ok, "the product carries " + category + "/");
    }
    Check(
        Canonical(Engine::BuiltinAssetsDir()) == Canonical(Engine::DataRoot() / "builtin_assets"),
        "built-in assets are root-relative"
    );
    Check(
        Canonical(Engine::ProjectsDir()) == Canonical(Engine::DataRoot() / "projects"),
        "project templates are root-relative"
    );
    Check(
        Canonical(Engine::AssetsDir()) == Canonical(Engine::DataRoot() / "assets"),
        "sample content is root-relative"
    );
    Check(
        Canonical(Engine::TestDataDir()) == Canonical(Engine::DataRoot() / "test_data"),
        "test fixtures are root-relative"
    );

    // --- The explicit override wins over proximity ---------------------------
    // Pointing it at the same root is enough to prove precedence, and it leaves
    // the filesystem untouched.
    const std::string resolved_root = Canonical(expected_root).string();
    SDL_setenv_unsafe(k_data_root_env_var, resolved_root.c_str(), 1);
    Engine::ResetDataRootCache();
    {
        const Engine::DataRootResolution &resolution = Engine::ResolveDataRoot();
        Check(resolution.source == Engine::DataRootSource::Environment, "an explicit override wins");
        Check(
            Canonical(Engine::DataRoot()) == Canonical(expected_root),
            "the override names the data root"
        );
    }

    // --- A required directory that is missing throws, and says what is missing
    // `test_data/` is a real directory that is not itself a data root, so it
    // resolves as one and then lacks every category: no writing involved.
    const std::filesystem::path not_a_root = Canonical(expected_root / "test_data");
    SDL_setenv_unsafe(k_data_root_env_var, not_a_root.string().c_str(), 1);
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
                "the missing-directory diagnostic names the directory that is missing"
            );
            Check(
                message.find(k_data_root_env_var) != std::string::npos,
                "the missing-directory diagnostic names the resolution step"
            );
        }
        Check(threw, "a root without the required directory throws");
    }

    // Leave the process with the default resolution for any later work.
    SDL_unsetenv_unsafe(k_data_root_env_var);
    Engine::ResetDataRootCache();

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "data root locator checks passed\n";
    return 0;
}
