#include <Core/FileSystem/DataRoot.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <Render/Asset/Shader/ShaderCompiler.h>

namespace {
    /// @brief The directory holding the built-in GLSL sources for this checkout.
    ///
    /// A staged data root that carries baked archives has no GLSL sources by
    /// design, and this test's whole point is to compile them, so it reads the
    /// source tree rather than the resolved data root.
    ///
    /// @return Absolute path of `builtin_assets/shaders`.
    std::filesystem::path SourceShadersDirectory() {
#ifdef NEEDRENAME_SOURCE_BUILTIN_ASSETS
        return std::filesystem::path{NEEDRENAME_SOURCE_BUILTIN_ASSETS} / "shaders";
#else
        return Engine::BuiltinAssetsDir() / "shaders";
#endif
    }

    /// @brief Collect every built-in shader entry point.
    ///
    /// Only the top level of `shaders/` is scanned: everything under
    /// `shaders/include/` is a header included by those entry points and has no
    /// entry point of its own.
    ///
    /// @return Absolute paths of the shaders, sorted for a deterministic report.
    std::vector<std::filesystem::path> CollectBuiltinShaders() {
        const std::filesystem::path directory = SourceShadersDirectory();
        std::vector<std::filesystem::path> shaders;
        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".glsl") continue;
            shaders.push_back(std::filesystem::absolute(entry.path()));
        }
        std::sort(shaders.begin(), shaders.end());
        return shaders;
    }
} // namespace

int main() {
    // The include tree travels with the sources, so it is named here too: a
    // product that ships baked archives carries neither, and the includer's own
    // system path would point at a directory that does not exist.
    Engine::ShaderCompiler compiler{SourceShadersDirectory() / "include"};
    const std::vector<std::filesystem::path> shaders = CollectBuiltinShaders();
    if (shaders.empty()) {
        std::cerr << "No built-in shaders found under " << SourceShadersDirectory() << "\n";
        return 1;
    }

    std::vector<std::string> failures;
    for (const auto &shader : shaders) {
        std::vector<uint32_t> spirv;
        // Debug information changes only the emitted SPIR-V, not what the front
        // end accepts, so it is skipped to keep the test fast.
        if (compiler.CompileGLSLtoSPV(spirv, shader, false)) continue;
        failures.push_back(shader.filename().string());
        std::cerr << "FAILED: " << shader.filename().string() << '\n';
    }

    if (!failures.empty()) {
        std::cerr << failures.size() << " of " << shaders.size() << " built-in shader(s) failed to compile:\n";
        for (const auto &failure : failures) {
            std::cerr << "  " << failure << '\n';
        }
        return 1;
    }

    std::cout << "All " << shaders.size() << " built-in shaders compiled.\n";
    return 0;
}
