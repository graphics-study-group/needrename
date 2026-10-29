#include <Render/Asset/Shader/ShaderCompiler.h>

#include <AnnoRefl/Archive.h>
#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
    constexpr const char *k_shader_asset_type = "Engine::ShaderAsset";
    constexpr const char *k_store_type_key = "ShaderAsset::storeType";
    constexpr const char *k_shader_type_key = "ShaderAsset::shaderType";
    constexpr const char *k_guid_key = "Asset::m_guid";
    constexpr const char *k_glsl_extra_data_id_key = "glsl_extra_data_id";
    constexpr const char *k_binary_extra_data_id_key = "binary_extra_data_id";

    struct Options {
        std::filesystem::path shaders_directory{};
        bool emit_debug_info = false;
    };

    [[noreturn]] void ThrowUsage(const std::string &message) {
        throw std::runtime_error(
            message
            + "\nUsage: shader_baker <shaders-directory> [--debug-info]\n"
              "  Rewrites every built-in ShaderAsset archive in <shaders-directory> from\n"
              "  GLSL form to embedded-SPIR-V form, preserving each asset's GUID."
        );
    }

    Options ParseArguments(int argc, char **argv) {
        Options options{};
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--debug-info") {
                options.emit_debug_info = true;
            } else if (argument.starts_with("--")) {
                ThrowUsage("Unknown option: " + std::string(argument));
            } else if (options.shaders_directory.empty()) {
                options.shaders_directory = std::filesystem::path(argument);
            } else {
                ThrowUsage("Unexpected argument: " + std::string(argument));
            }
        }
        if (options.shaders_directory.empty()) {
            ThrowUsage("Missing shader directory argument.");
        }
        return options;
    }

    std::string ReadFile(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + path.string());
        }
        const auto size = static_cast<size_t>(file.tellg());
        std::string content(size, '\0');
        file.seekg(0, std::ios::beg);
        file.read(content.data(), static_cast<std::streamsize>(size));
        return content;
    }

    void WriteFile(const std::filesystem::path &path, const void *data, size_t size) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot write file: " + path.string());
        }
        file.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
        if (!file) {
            throw std::runtime_error("Failed while writing file: " + path.string());
        }
    }

    /// @brief Get the engine archive base path of an `.asset` file (the path without `.asset`).
    std::filesystem::path ArchiveBasePath(const std::filesystem::path &asset_path) {
        std::filesystem::path base = asset_path;
        base.replace_extension();
        return base;
    }

    /// @brief Validate the shader-type spelling against the file name the runtime compiler infers from.
    void CheckShaderTypeMatchesFileName(const std::string &shader_type, const std::filesystem::path &glsl_path) {
        struct TypeToken {
            const char *shader_type;
            const char *file_token;
        };
        static constexpr TypeToken tokens[] = {
            {"Vertex", "vert"},
            {"Fragment", "frag"},
            {"Compute", "comp"},
            {"TessellationControl", "tesc"},
            {"TessellationEvaluation", "tese"},
            {"Geometry", "geom"},
        };
        const std::string file_name = glsl_path.filename().string();
        for (const TypeToken &token : tokens) {
            if (shader_type != token.shader_type) {
                continue;
            }
            if (file_name.find(token.file_token) == std::string::npos) {
                throw std::runtime_error(
                    "Shader type '" + shader_type + "' does not match the file name '" + file_name
                    + "': the runtime compiler would infer a different stage."
                );
            }
            return;
        }
        throw std::runtime_error("Unsupported shader type: " + shader_type);
    }

    /// @brief Bake one `.asset` archive in place. Returns false when the file is not a shader asset.
    bool BakeShaderAsset(
        const std::filesystem::path &asset_path, bool emit_debug_info, Engine::ShaderCompiler &compiler
    ) {
        const std::filesystem::path base_path = ArchiveBasePath(asset_path);
        const std::string archive_text = ReadFile(asset_path);
        nlohmann::json archive_json = nlohmann::json::parse(archive_text);

        if (!archive_json.contains("%main_data") || !archive_json["%main_data"].contains("%type")
            || archive_json["%main_data"]["%type"].get<std::string>() != k_shader_asset_type) {
            return false;
        }

        nlohmann::json &main_data = archive_json["%main_data"];
        const std::string store_type = main_data.value(k_store_type_key, std::string{"GLSL"});
        if (store_type == "SPIRV") {
            std::cout << "  skip (already SPIR-V): " << asset_path.filename().string() << '\n';
            return true;
        }
        if (store_type != "GLSL") {
            throw std::runtime_error("Unknown store type '" + store_type + "' in " + asset_path.string());
        }

        const size_t glsl_extra_data_id = main_data.value(k_glsl_extra_data_id_key, size_t{0});
        const size_t binary_extra_data_id = main_data.value(k_binary_extra_data_id_key, size_t{1});
        const std::filesystem::path glsl_path = base_path.string() + "." + std::to_string(glsl_extra_data_id) + ".glsl";
        if (!std::filesystem::is_regular_file(glsl_path)) {
            throw std::runtime_error("Shader asset has no GLSL source: " + glsl_path.string());
        }
        CheckShaderTypeMatchesFileName(main_data.value(k_shader_type_key, std::string{}), glsl_path);

        std::vector<uint32_t> spirv;
        if (!compiler.CompileGLSLtoSPV(spirv, std::filesystem::absolute(glsl_path), emit_debug_info)) {
            throw std::runtime_error("Failed to compile shader: " + glsl_path.string());
        }
        if (spirv.empty()) {
            throw std::runtime_error("Shader compiled to an empty SPIR-V binary: " + glsl_path.string());
        }

        // The archive keeps its identity: GUID, name, entry point, shader type and
        // extra-data index layout are untouched. Only the stored form changes.
        const std::string guid_before = main_data.value(k_guid_key, std::string{});
        main_data[k_store_type_key] = "SPIRV";
        if (main_data.value(k_guid_key, std::string{}) != guid_before) {
            throw std::runtime_error("GUID changed while baking: " + asset_path.string());
        }

        const std::filesystem::path binary_path =
            base_path.string() + "." + std::to_string(binary_extra_data_id) + ".spv";
        WriteFile(binary_path, spirv.data(), spirv.size() * sizeof(uint32_t));

        // The GLSL source is no longer part of the baked archive.
        std::error_code ec;
        std::filesystem::remove(glsl_path, ec);

        std::ofstream asset_file(asset_path, std::ios::trunc);
        if (!asset_file.is_open()) {
            throw std::runtime_error("Cannot write shader asset: " + asset_path.string());
        }
        asset_file << archive_json.dump(4) << std::endl;

        std::cout << "  baked " << asset_path.filename().string() << " (" << spirv.size() << " words)\n";
        return true;
    }
} // namespace

int main(int argc, char **argv) {
    try {
        const Options options = ParseArguments(argc, argv);
        if (!std::filesystem::is_directory(options.shaders_directory)) {
            throw std::runtime_error("Not a directory: " + options.shaders_directory.string());
        }

        SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN);
        Engine::ShaderCompiler compiler;

        std::cout << "Baking built-in shaders in " << options.shaders_directory.string() << '\n';
        size_t baked_count = 0;
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::directory_iterator(options.shaders_directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".asset") {
                continue;
            }
            if (BakeShaderAsset(entry.path(), options.emit_debug_info, compiler)) {
                ++baked_count;
            }
        }
        std::cout << "Done: " << baked_count << " shader asset(s) processed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "shader_baker: " << error.what() << '\n';
        return 1;
    }
}
