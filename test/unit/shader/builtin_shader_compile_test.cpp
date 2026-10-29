#include <Core/FileSystem/DataRoot.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <Render/Asset/Shader/ShaderCompiler.h>

namespace {
    /// @brief Collect every built-in shader entry point.
    ///
    /// Only the top level of `shaders/` is scanned: everything under
    /// `shaders/include/` is a header included by those entry points and has no
    /// entry point of its own.
    ///
    /// @return Absolute paths of the shaders, sorted for a deterministic report.
    std::vector<std::filesystem::path> CollectBuiltinShaders() {
        const std::filesystem::path directory = Engine::BuiltinAssetsDir() / "shaders";
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
    Engine::ShaderCompiler compiler;
    const std::vector<std::filesystem::path> shaders = CollectBuiltinShaders();
    if (shaders.empty()) {
        std::cerr << "No built-in shaders found under " << Engine::BuiltinAssetsDir() << "/shaders\n";
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
