#include <Render/Asset/Shader/ShaderCompiler.h>

#include "wgsl_bake.h"

#include <SDL3/SDL.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
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
        /// @brief Directory of SPIR-V modules to bake the WGSL variant for.
        std::filesystem::path spirv_directory{};
        /// @brief Single GLSL source to compile (the physics shader pipeline's mode).
        std::filesystem::path source_file{};
        /// @brief Output base path for --source: outputs land at <base>.spv and friends.
        std::filesystem::path output_base{};
        /// @brief Makefile-compatible dependency file for the single-source mode.
        std::filesystem::path depfile{};
        /// @brief Alongside the SPIR-V, bake the WGSL variant and interface description.
        bool emit_wgsl = false;
        bool emit_debug_info = false;
        /// Bake an archive that already stores SPIR-V again, from its GLSL source. The stored form
        /// carries the interface description, and a description written by an older baker stays
        /// stale until the asset is baked again.
        bool force = false;
        /// @brief Converter invoked per module; the WGSL bake is skipped when empty.
        std::string converter_executable{};
    };
    [[noreturn]] void ThrowUsage(const std::string &message) {
        throw std::runtime_error(
            message
            + "\nUsage: shader_baker <shaders-directory> [--debug-info]\n"
              "       shader_baker --spirv-dir <directory> [--converter <path>]\n"
              "       shader_baker --source <file.glsl> --output-base <path> [--wgsl]\n"
              "                  [--converter <path>] [--debug-info] [--depfile <path>]\n"
              "  With a shader directory: rewrites every built-in ShaderAsset archive from\n"
              "  GLSL form to embedded-SPIR-V form, preserving each asset's GUID.\n"
              "  With --spirv-dir: writes the WGSL variant and the interface description\n"
              "  next to every SPIR-V module in the tree, which is what the WebGPU backend\n"
              "  loads instead of reflecting.\n"
              "  With --source: compiles one GLSL source to <path>.spv, and with --wgsl also\n"
              "  to <path>.wgsl and <path>.iface.json through the same rewrite, conversion\n"
              "  and description code as the built-in asset bake.\n"
        );
    }

    Options ParseArguments(int argc, char **argv) {
        Options options{};
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--force") {
                options.force = true;
                continue;
            }
            if (argument == "--debug-info") {
                options.emit_debug_info = true;
            } else if (argument == "--spirv-dir") {
                if (index + 1 >= argc) {
                    ThrowUsage("--spirv-dir needs a directory argument.");
                }
                options.spirv_directory = std::filesystem::path(argv[++index]);
            } else if (argument == "--source") {
                if (index + 1 >= argc) {
                    ThrowUsage("--source needs a file argument.");
                }
                options.source_file = std::filesystem::path(argv[++index]);
            } else if (argument == "--output-base") {
                if (index + 1 >= argc) {
                    ThrowUsage("--output-base needs a path argument.");
                }
                options.output_base = std::filesystem::path(argv[++index]);
            } else if (argument == "--depfile") {
                if (index + 1 >= argc) {
                    ThrowUsage("--depfile needs a path argument.");
                }
                options.depfile = std::filesystem::path(argv[++index]);
            } else if (argument == "--wgsl") {
                options.emit_wgsl = true;
            } else if (argument == "--converter") {
                if (index + 1 >= argc) {
                    ThrowUsage("--converter needs an executable path.");
                }
                options.converter_executable = argv[++index];
            } else if (argument.starts_with("--")) {
                ThrowUsage("Unknown option: " + std::string(argument));
            } else if (options.shaders_directory.empty()) {
                options.shaders_directory = std::filesystem::path(argument);
            } else {
                ThrowUsage("Unexpected argument: " + std::string(argument));
            }
        }
        if (options.shaders_directory.empty() && options.spirv_directory.empty() && options.source_file.empty()) {
            ThrowUsage("Missing shader directory, SPIR-V directory, or source argument.");
        }
        if (!options.source_file.empty() && options.output_base.empty()) {
            ThrowUsage("--source needs --output-base naming the output path without extension.");
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

    std::string DepfilePath(const std::filesystem::path &path) {
        const std::string input = std::filesystem::absolute(path).generic_string();
        std::string escaped;
        escaped.reserve(input.size());
        for (const char character : input) {
            if (character == ' ' || character == '#') {
                escaped.push_back('\\');
            } else if (character == '$') {
                escaped.push_back('$');
            }
            escaped.push_back(character);
        }
        return escaped;
    }

    void WriteDepfile(
        const std::filesystem::path &path,
        const std::vector<std::filesystem::path> &outputs,
        const std::filesystem::path &source,
        const std::set<std::string> &included_files
    ) {
        std::string content;
        for (const auto &output : outputs) {
            content += DepfilePath(output) + " ";
        }
        content += ":";
        content += " " + DepfilePath(source);
        for (const auto &included : included_files) {
            content += " " + DepfilePath(included);
        }
        content += "\n";
        WriteFile(path, content.data(), content.size());
    }

    /// @brief Get the engine archive base path of an `.asset` file (the path without `.asset`).
    std::filesystem::path ArchiveBasePath(const std::filesystem::path &asset_path) {
        std::filesystem::path base = asset_path;
        base.replace_extension();
        return base;
    }

    /// @brief Lower-case spelling of a shader-type name, as the interface description records it.
    std::string LowerCase(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return text;
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
        const std::filesystem::path &asset_path,
        bool emit_debug_info,
        bool force,
        Engine::ShaderCompiler &compiler,
        const std::string &converter_executable
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
        if (store_type == "SPIRV" && !force) {
            std::cout << "  skip (already SPIR-V): " << asset_path.filename().string() << '\n';
            return true;
        }
        if (store_type != "SPIRV" && store_type != "GLSL") {
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

        // The WebGPU runtime consumes WGSL text, not SPIR-V, and cannot reflect a module whose
        // bindings a converter had to restate. Both facts are produced here and stored in the
        // same archive as the SPIR-V form, so one asset set serves both backends.
        if (!converter_executable.empty()) {
            std::string wgsl;
            std::string wgsl_error;
            if (!Engine::ShaderBake::ConvertSpirvToWgsl(
                    spirv, converter_executable, wgsl, wgsl_error, asset_path.parent_path()
                )) {
                throw std::runtime_error(
                    "Failed to convert " + asset_path.filename().string() + " to WGSL: " + wgsl_error
                );
            }
            size_t wgsl_extra_data_id = main_data.value("wgsl_extra_data_id", std::numeric_limits<size_t>::max());
            if (wgsl_extra_data_id == std::numeric_limits<size_t>::max()) {
                archive_json["%extra_data"].push_back({{"%extension", std::string{".wgsl"}}});
                wgsl_extra_data_id = archive_json["%extra_data"].size() - 1;
                main_data["wgsl_extra_data_id"] = wgsl_extra_data_id;
            }
            const std::filesystem::path wgsl_path =
                base_path.string() + "." + std::to_string(wgsl_extra_data_id) + ".wgsl";
            WriteFile(wgsl_path, wgsl.data(), wgsl.size());

            // The description is what a runtime that cannot reflect binds against; it records
            // the binding table after the bake's rewrites, including where a combined sampler's
            // sampler landed and where the constant block was moved to.
            std::string description;
            const std::string shader_stage = main_data.value(k_shader_type_key, std::string{"Unknown"});
            if (Engine::ShaderBake::WriteInterfaceDescriptionForModule(
                    spirv, asset_path.stem().string(), LowerCase(shader_stage), wgsl, description
                )) {
                size_t description_extra_data_id =
                    main_data.value("iface_extra_data_id", std::numeric_limits<size_t>::max());
                if (description_extra_data_id == std::numeric_limits<size_t>::max()) {
                    archive_json["%extra_data"].push_back({{"%extension", std::string{".iface.json"}}});
                    description_extra_data_id = archive_json["%extra_data"].size() - 1;
                    main_data["iface_extra_data_id"] = description_extra_data_id;
                }
                const std::filesystem::path description_path =
                    base_path.string() + "." + std::to_string(description_extra_data_id) + ".iface.json";
                WriteFile(description_path, description.data(), description.size());
            }
        }

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
        SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN);

        // WGSL bake: the WebGPU runtime's artifacts, next to the modules they
        // were derived from. Kept separate from the archive bake below because
        // the physics shader tree is a plain SPIR-V tree with no archives.
        if (!options.spirv_directory.empty()) {
            Engine::ShaderBake::WgslBakeOptions wgsl_options{};
            wgsl_options.spirv_directory = options.spirv_directory;
            wgsl_options.converter_executable =
                options.converter_executable.empty() ? Engine::ShaderBake::DefaultConverterExecutable()
                                                     : options.converter_executable;
            std::cout << "Baking WGSL variants in " << options.spirv_directory.string() << " with "
                      << wgsl_options.converter_executable << '\n';
            return Engine::ShaderBake::BakeWgslTree(wgsl_options);
        }

        // Single-source compile (the physics shader pipeline): SPIR-V, plus the
        // WGSL variant and interface description for a WebGPU build, through the
        // same compile, reflect, rewrite, convert and description code the other
        // modes use. A failure exits nonzero with the source path in the
        // diagnostic and leaves no output behind: each file is written only after
        // the step that produces it succeeded, and a failed run removes what an
        // earlier step of the same invocation wrote.
        if (!options.source_file.empty()) {
            if (options.emit_wgsl && options.converter_executable.empty()) {
                throw std::runtime_error("--wgsl needs --converter naming a tint executable.");
            }
            const std::filesystem::path source_absolute = std::filesystem::absolute(options.source_file);
            Engine::ShaderCompiler compiler;
            std::vector<uint32_t> spirv;
            std::set<std::string> included_files;
            if (!compiler.CompileGLSLtoSPV(spirv, source_absolute, options.emit_debug_info, &included_files)) {
                throw std::runtime_error("Failed to compile shader: " + source_absolute.string());
            }
            if (spirv.empty()) {
                throw std::runtime_error("Shader compiled to an empty SPIR-V binary: " + source_absolute.string());
            }

            const std::filesystem::path spirv_path = options.output_base.string() + ".spv";
            std::error_code directory_error;
            std::filesystem::create_directories(options.output_base.parent_path(), directory_error);
            WriteFile(spirv_path, spirv.data(), spirv.size() * sizeof(uint32_t));
            std::cout << "  compiled " << source_absolute.filename().string() << " -> " << spirv_path.string() << " ("
                      << spirv.size() << " words)\n";

            std::vector<std::filesystem::path> outputs = {spirv_path};
            if (options.emit_wgsl) {
                Engine::ShaderBake::WgslBakeOptions wgsl_options{};
                wgsl_options.converter_executable = options.converter_executable;
                std::string wgsl_error;
                if (!Engine::ShaderBake::BakeWgslModule(wgsl_options, spirv_path, wgsl_error)) {
                    // A failed bake must not leave outputs a later build accepts: remove the
                    // SPIR-V this invocation wrote (the WGSL bake cleans its own intermediates).
                    std::error_code remove_error;
                    std::filesystem::remove(spirv_path, remove_error);
                    std::filesystem::remove(options.output_base.string() + ".wgsl", remove_error);
                    std::filesystem::remove(options.output_base.string() + ".iface.json", remove_error);
                    throw std::runtime_error(
                        "Failed to bake the WGSL variant of " + source_absolute.string() + ": " + wgsl_error
                    );
                }
                outputs.emplace_back(options.output_base.string() + ".wgsl");
                outputs.emplace_back(options.output_base.string() + ".iface.json");
            }
            if (!options.depfile.empty()) {
                WriteDepfile(options.depfile, outputs, source_absolute, included_files);
            }
            return 0;
        }

        if (!std::filesystem::is_directory(options.shaders_directory)) {
            throw std::runtime_error("Not a directory: " + options.shaders_directory.string());
        }

        // The tooling build has no runtime data root, so the built-in shader
        // include tree is registered the same way the runtime includer does it:
        // as a system include path next to the shader directory being baked
        // (the shaders directory is builtin_assets/shaders; its include tree is
        // the runtime's <builtin_assets>/shaders/include).
        Engine::ShaderCompiler compiler(options.shaders_directory / "include");

        std::cout << "Baking built-in shaders in " << options.shaders_directory.string() << '\n';
        size_t baked_count = 0;
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::directory_iterator(options.shaders_directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".asset") {
                continue;
            }
            if (BakeShaderAsset(
                    entry.path(), options.emit_debug_info, options.force, compiler, options.converter_executable
                )) {
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
