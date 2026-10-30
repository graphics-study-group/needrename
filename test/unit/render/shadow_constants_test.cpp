#include <Core/FileSystem/DataRoot.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "Render/RenderSystem/SceneDataManager.h"

using Engine::RenderSystemState::SceneDataManager;

namespace {
    /// @brief Number of failures reported so far.
    int failures = 0;

    void Check(bool condition, const std::string &what) {
        if (condition) return;
        std::cerr << "FAIL: " << what << '\n';
        failures++;
    }

    /**
     * @brief Read the value of a `#define <name> <value>` line from a shader header.
     *
     * @param path Absolute path of the header.
     * @param name Macro name of the constant.
     * @return The value token, or an empty string when the header defines no such macro.
     */
    std::string ReadDefine(const std::filesystem::path &path, const std::string &name) {
        std::ifstream file(path);
        Check(file.is_open(), path.filename().string() + " opens");
        if (!file.is_open()) return {};

        const std::string prefix = "#define " + name + " ";
        std::string line;
        while (std::getline(file, line)) {
            if (line.rfind(prefix, 0) != 0) continue;
            std::string value = line.substr(prefix.size());
            while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
            return value;
        }
        return {};
    }

    /**
     * @brief Check that a shader header defines a constant with the engine's value.
     *
     * The shader interface is written by hand, so the two sides of the
     * uniform layout can drift apart silently. `SHADOWMAP_SIZE` is the dangerous
     * one: the shader derives its PCF radius and normal offset from it, so a
     * mismatch only makes shadows slightly wrong instead of failing loudly.
     */
    void CheckDefineEquals(const std::filesystem::path &path, const std::string &name, uint32_t expected) {
        const std::string value = ReadDefine(path, name);
        if (value.empty()) {
            Check(false, path.filename().string() + " defines " + name);
            return;
        }
        const unsigned long parsed = std::strtoul(value.c_str(), nullptr, 10);
        Check(
            parsed == expected,
            name + " in " + path.filename().string() + " is " + std::to_string(expected) + " (found " + value + ")"
        );
    }

    /**
     * @brief Check that a shader header defines a floating point constant with the engine's value.
     */
    void CheckFloatDefineEquals(const std::filesystem::path &path, const std::string &name, float expected) {
        const std::string value = ReadDefine(path, name);
        if (value.empty()) {
            Check(false, path.filename().string() + " defines " + name);
            return;
        }
        const float parsed = std::strtof(value.c_str(), nullptr);
        Check(
            parsed == expected,
            name + " in " + path.filename().string() + " is " + std::to_string(expected) + " (found " + value + ")"
        );
    }
} // namespace

int main() {
    const std::filesystem::path include_directory = Engine::BuiltinAssetsDir() / "shaders" / "include" / "engine";
    const std::filesystem::path interface_header = include_directory / "interface.glsl";
    const std::filesystem::path shadow_header = include_directory / "shadow.glsl";

    // Light and camera counts size the uniform block, so they have to match the
    // host's allocation exactly.
    CheckDefineEquals(interface_header, "MAX_SHADOW_CASTING_LIGHTS", SceneDataManager::MAX_SHADOW_CASTING_LIGHTS);
    CheckDefineEquals(interface_header, "MAX_SHADOW_CASCADES", SceneDataManager::MAX_SHADOW_CASCADES);
    CheckDefineEquals(
        interface_header, "MAX_NON_SHADOW_CASTING_LIGHTS", SceneDataManager::MAX_NON_SHADOW_CASTING_LIGHTS
    );

    // The shadow sampling turns texel counts into world and uv offsets with this one.
    CheckFloatDefineEquals(shadow_header, "SHADOWMAP_SIZE", static_cast<float>(SceneDataManager::SHADOWMAP_SIZE));

    if (failures != 0) {
        std::cerr << failures << " shader constant(s) disagree with the engine.\n";
        return 1;
    }
    std::cout << "Shader constants agree with the engine.\n";
    return 0;
}
