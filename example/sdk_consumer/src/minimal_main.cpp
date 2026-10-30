#include <Core/FileSystem/DataRoot.h>

#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>

namespace {
    namespace fs = std::filesystem;

    /// @brief The shader storage forms found beneath the built-in assets.
    struct ShaderForm {
        std::size_t archives = 0;
        std::size_t glsl_sources = 0;
        std::size_t spirv_archives = 0;
    };

    /// @brief Check whether an asset archive stores its shader as SPIR-V.
    bool StoresSpirv(const fs::path &asset_path) {
        std::ifstream file(asset_path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // The archive key is qualified (`"ShaderAsset::storeType"`), so only the
        // suffix is matched. A SPIR-V archive also drops its sibling `.glsl`.
        return content.find("storeType\": \"SPIRV\"") != std::string::npos
               || content.find("storeType\":\"SPIRV\"") != std::string::npos;
    }

    /// @brief Count the shader archives and the storage form they carry.
    ShaderForm ScanShaderForm(const fs::path &directory) {
        ShaderForm form{};
        std::error_code error;
        for (fs::recursive_directory_iterator it(directory, error), end; it != end; it.increment(error)) {
            if (error) {
                break;
            }
            if (!it->is_regular_file(error)) {
                continue;
            }
            const fs::path &path = it->path();
            if (path.extension() == ".asset") {
                ++form.archives;
                if (StoresSpirv(path)) {
                    ++form.spirv_archives;
                }
            } else if (path.extension() == ".glsl") {
                ++form.glsl_sources;
            }
        }
        return form;
    }
} // namespace

int main() {
    try {
        const Engine::DataRootResolution &resolution = Engine::ResolveDataRoot();
        std::cout << "data root          : " << resolution.root.string() << '\n';
        std::cout << "resolved through   : " << resolution.step << '\n';

        // Each accessor throws when its directory is absent, so this doubles as
        // a check that the installed layout is complete.
        const std::array<std::pair<const char *, fs::path>, 3> required{{
            {"builtin_assets", Engine::BuiltinAssetsDir()},
            {"projects", Engine::ProjectsDir()},
            {"assets", Engine::AssetsDir()},
        }};
        for (const auto &[name, path] : required) {
            std::cout << "  " << name << " : " << path.string() << '\n';
        }
        try {
            std::cout << "  test_data : " << Engine::TestDataDir().string() << '\n';
        } catch (const std::exception &) {
            std::cout << "  test_data : (not installed)\n";
        }

        const fs::path builtin_assets = Engine::BuiltinAssetsDir();
        const fs::path shaders = builtin_assets / "shaders";
        const ShaderForm form = ScanShaderForm(fs::is_directory(shaders) ? shaders : builtin_assets);
        std::cout << "shader archives    : " << form.archives << " (SPIR-V: " << form.spirv_archives
                  << ", GLSL sources: " << form.glsl_sources << ")\n";
        std::cout << "shader storage form: "
                  << (form.archives > 0 && form.glsl_sources == 0 && form.spirv_archives > 0
                          ? "baked SPIR-V"
                          : "GLSL (compiled at runtime)")
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
