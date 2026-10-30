#include <Core/FileSystem/DataRoot.h>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {
    constexpr const char *k_data_root_env_var = "NEEDRENAME_DATA_ROOT";
    constexpr const char *k_shader_asset_type = "Engine::ShaderAsset";

    int g_failures = 0;

    void Check(bool condition, const std::string &message) {
        if (condition) {
            return;
        }
        std::cerr << "FAILED: " << message << '\n';
        ++g_failures;
    }

    nlohmann::json ReadJson(const std::filesystem::path &path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open " + path.string());
        }
        nlohmann::json json;
        file >> json;
        return json;
    }

    std::filesystem::path ExtraDataPath(
        const std::filesystem::path &asset_path,
        const nlohmann::json &main_data,
        const char *id_key,
        const char *extension
    ) {
        std::filesystem::path base = asset_path;
        base.replace_extension();
        const size_t id = main_data.value(id_key, size_t{0});
        return base.string() + "." + std::to_string(id) + extension;
    }

    bool FileHasContent(const std::filesystem::path &path) {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec) && std::filesystem::file_size(path, ec) > 0;
    }
} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: shader_archive_form_test <data-root> <glsl|spv>\n";
        return 2;
    }

    const std::filesystem::path data_root(argv[1]);
    const std::string expected_mode(argv[2]);
    if (expected_mode != "glsl" && expected_mode != "spv") {
        std::cerr << "unexpected mode: " << expected_mode << '\n';
        return 2;
    }

    // Point the locator at the staged data root before anything resolves it.
    SDL_setenv_unsafe(k_data_root_env_var, data_root.string().c_str(), 1);
    Engine::ResetDataRootCache();

    Check(
        Engine::ResolveDataRoot().source == Engine::DataRootSource::Environment,
        "the staged data root resolves through the environment override"
    );

    const std::filesystem::path shaders_dir = Engine::BuiltinAssetsDir() / "shaders";
    Check(std::filesystem::is_directory(shaders_dir), "the staged data root has a shader directory");

    const bool expect_spv = expected_mode == "spv";
    size_t shader_assets = 0;
    std::set<std::string> guids;
    std::error_code ec;

    for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(shaders_dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".asset") {
            continue;
        }
        const nlohmann::json archive = ReadJson(entry.path());
        if (!archive.contains("%main_data")
            || archive["%main_data"].value("%type", std::string{}) != k_shader_asset_type) {
            continue;
        }
        ++shader_assets;
        const nlohmann::json &main_data = archive["%main_data"];

        const std::string guid = main_data.value("Asset::m_guid", std::string{});
        Check(!guid.empty(), entry.path().filename().string() + " keeps its GUID");
        Check(guids.insert(guid).second, entry.path().filename().string() + " has a unique GUID");

        const std::string store_type = main_data.value("ShaderAsset::storeType", std::string{});
        Check(
            store_type == (expect_spv ? "SPIRV" : "GLSL"),
            entry.path().filename().string() + " stores " + (expect_spv ? "SPIRV" : "GLSL") + " (got " + store_type
                + ")"
        );

        if (expect_spv) {
            const std::filesystem::path binary = ExtraDataPath(entry.path(), main_data, "binary_extra_data_id", ".spv");
            Check(FileHasContent(binary), binary.filename().string() + " embeds a non-empty SPIR-V binary");
            if (FileHasContent(binary)) {
                Check(
                    std::filesystem::file_size(binary, ec) % sizeof(uint32_t) == 0,
                    binary.filename().string() + " has a whole number of SPIR-V words"
                );
            }
        } else {
            const std::filesystem::path source = ExtraDataPath(entry.path(), main_data, "glsl_extra_data_id", ".glsl");
            Check(FileHasContent(source), source.filename().string() + " ships the GLSL source");
        }
    }

    Check(shader_assets > 0, "the staged data root contains shader assets");

    if (expect_spv) {
        size_t glsl_sources = 0;
        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(shaders_dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".glsl") {
                ++glsl_sources;
            }
        }
        Check(glsl_sources == 0, "spv mode ships no built-in GLSL sources");
        Check(!std::filesystem::exists(shaders_dir / "include"), "spv mode ships no shader include tree");
    } else {
        Check(
            FileHasContent(shaders_dir / "include" / "engine" / "interface.glsl"),
            "glsl mode ships the shader include tree"
        );
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "shader archive form checks passed (" << shader_assets << " shader assets, mode " << expected_mode
              << ")\n";
    return 0;
}
