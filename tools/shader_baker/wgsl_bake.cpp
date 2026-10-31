#include "wgsl_bake.h"

#include "spirv_edit.h"

#include <Rhi/Pipeline/ShaderInterface.h>
#include <Rhi/Pipeline/ShaderParameterLayout.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

namespace Engine::ShaderBake {
    namespace {
        /// @brief Read a binary file into SPIR-V words.
        bool ReadSpirv(const std::filesystem::path &path, std::vector<uint32_t> &words) {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                return false;
            }
            const auto size = static_cast<size_t>(file.tellg());
            if (size == 0 || size % sizeof(uint32_t) != 0) {
                return false;
            }
            words.resize(size / sizeof(uint32_t));
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size));
            return static_cast<bool>(file);
        }

        /// @brief Write SPIR-V words to a binary file.
        bool WriteSpirv(const std::filesystem::path &path, const std::vector<uint32_t> &words) {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                return false;
            }
            file.write(
                reinterpret_cast<const char *>(words.data()), static_cast<std::streamsize>(words.size() * sizeof(uint32_t))
            );
            return static_cast<bool>(file);
        }

        /// @brief Quotes a path for the command line of the converter.
        std::string Quote(const std::filesystem::path &path) {
            return "\"" + path.string() + "\"";
        }

        /// @brief The shell's way of discarding a program's output.
        constexpr const char *k_silence =
#ifdef _WIN32
            " > NUL 2>&1";
#else
            " > /dev/null 2>&1";
#endif

        /// @brief Get the first diagnostic line of a log file, for quoting back in an error.
        std::string FirstLogLine(const std::filesystem::path &log_path) {
            std::ifstream log_file(log_path);
            if (!log_file.is_open()) {
                return "no diagnostic was produced";
            }
            std::string first_non_empty;
            std::string line;
            while (std::getline(log_file, line)) {
                if (line.empty() || line.find_first_not_of(" \t\r") == std::string::npos) {
                    continue;
                }
                // A converter usually prints a summary line and then the actual
                // reason, so prefer the line that names an error.
                first_non_empty = line;
            }
            return first_non_empty.empty() ? "the converter reported no reason" : first_non_empty;
        }

        /// @brief Run a command and return its exit code.
        int Run(const std::string &command) {
#ifdef _WIN32
            // `cmd /c` strips the outermost quotes, so a command whose first
            // token is itself quoted needs a second pair around the whole thing
            // or the shell mis-parses the executable path.
            const std::string wrapped = "\"" + command + "\"";
            return std::system(wrapped.c_str()) == 0 ? 0 : 1;
#else
            return std::system(command.c_str()) == 0 ? 0 : 1;
#endif
        }

        /// @brief The kind of interface a description entry records.
        const char *DescribeKind(const Engine::Rhi::SPInterface &interface) {
            using namespace Engine::Rhi;
            if (const auto *structured = dynamic_cast<const SPInterfaceStructuredBuffer *>(&interface)) {
                return structured->type == SPInterfaceBuffer::Type::StorageBuffer ? "storage-buffer" : "uniform-buffer";
            }
            if (const auto *buffer = dynamic_cast<const SPInterfaceBuffer *>(&interface)) {
                return buffer->type == SPInterfaceBuffer::Type::StorageBuffer ? "storage-buffer" : "uniform-buffer";
            }
            if (dynamic_cast<const SPInterfaceOpaqueStorageImage *>(&interface) != nullptr) {
                return "storage-image";
            }
            if (const auto *image = dynamic_cast<const SPInterfaceOpaqueImage *>(&interface)) {
                const bool has_sampler = image->flags.Test(SPInterfaceOpaqueImage::ImageFlagBits::HasSampler);
                return has_sampler ? "combined-image-sampler" : "sampled-image";
            }
            if (const auto *other = dynamic_cast<const SPInterfaceOpaqueOther *>(&interface)) {
                return other->type == SPInterfaceOpaqueOther::Type::Sampler ? "sampler" : "unknown";
            }
            return "unknown";
        }

        /**
         * @brief Write the interface description the WebGPU runtime binds against.
         *
         * Everything the runtime needs to build a bind group and to size a
         * constant window is here, so the runtime never reflects a module.
         */
        bool WriteInterfaceDescription(
            const std::filesystem::path &path,
            const std::string &shader_name,
            const std::string &stage,
            const std::string &entry_point,
            const Engine::Rhi::SPLayout &layout,
            const ConstantBlockLayout &constant_block
        ) {
            std::ostringstream json;
            json << "{\n";
            json << "  \"version\": 1,\n";
            json << "  \"shader\": \"" << shader_name << "\",\n";
            json << "  \"stage\": \"" << stage << "\",\n";
            json << "  \"entry_point\": \"" << entry_point << "\",\n";
            if (constant_block.has_block) {
                json << "  \"constant_block\": { \"set\": " << constant_block.set << ", \"binding\": "
                     << constant_block.binding << ", \"size\": " << constant_block.size_bytes << " },\n";
            } else {
                json << "  \"constant_block\": null,\n";
            }
            json << "  \"bindings\": [\n";
            for (size_t index = 0; index < layout.interfaces.size(); ++index) {
                const Engine::Rhi::SPInterface &interface = *layout.interfaces[index];
                json << "    { \"set\": " << interface.layout_set << ", \"binding\": " << interface.layout_binding
                     << ", \"kind\": \"" << DescribeKind(interface) << "\", \"name\": \"" << interface.name << "\" }";
                json << (index + 1 == layout.interfaces.size() ? "\n" : ",\n");
            }
            json << "  ]\n";
            json << "}\n";

            std::ofstream file(path, std::ios::trunc);
            if (!file.is_open()) {
                return false;
            }
            file << json.str();
            return static_cast<bool>(file);
        }

        /// @brief Turns a module file name into the shader's logical name.
        std::string ShaderNameOf(const std::filesystem::path &module_path) {
            std::string name = module_path.stem().string();
            return name;
        }

        /// @brief The stage a module declares, inferred from its file name extension.
        std::string StageOf(const std::filesystem::path &module_path) {
            const std::string name = module_path.stem().string();
            if (name.find(".vert") != std::string::npos) {
                return "vertex";
            }
            if (name.find(".frag") != std::string::npos) {
                return "fragment";
            }
            if (name.find(".comp") != std::string::npos) {
                return "compute";
            }
            return "unknown";
        }

        /// @brief Bake one module: rewrite, convert, describe, validate.
        bool BakeModule(const WgslBakeOptions &options, const std::filesystem::path &module_path, std::string &error) {
            std::vector<uint32_t> spirv;
            if (!ReadSpirv(module_path, spirv)) {
                error = "cannot read module";
                return false;
            }

            // Reflection happens here, at bake time, and its answers are written
            // into the description. This is the only place a SPIR-V module is
            // ever reflected for the WebGPU runtime.
            Engine::Rhi::SPLayout layout = Engine::Rhi::SPLayout::Reflect(spirv, false);

            uint32_t highest_set = 0;
            for (const auto &interface : layout.interfaces) {
                highest_set = std::max(highest_set, interface->layout_set);
            }
            const uint32_t reserved_set = layout.interfaces.empty() ? 0 : highest_set + 1;
            constexpr uint32_t k_reserved_binding = 0;

            // Three rewrites make the module expressible in WGSL: the access
            // qualifiers WGSL does not have, the debug information WGSL cannot
            // carry, and the constant block's address space. Each is recorded in
            // the description where it changes what the runtime must bind.
            std::vector<uint32_t> widened = StripWriteOnlyDecorations(spirv, error);
            if (widened.empty()) {
                return false;
            }
            std::vector<uint32_t> undebugged = StripNonSemanticDebugInfo(widened, error);
            if (undebugged.empty()) {
                return false;
            }
            const std::vector<uint32_t> rewritten = FreezePushConstantsToUniform(
                undebugged, reserved_set, k_reserved_binding, layout.push_constant_size, error
            );
            if (rewritten.empty()) {
                return false;
            }
            ConstantBlockLayout constant_block{};
            constant_block.has_block = layout.push_constant_size != 0;
            constant_block.set = reserved_set;
            constant_block.binding = k_reserved_binding;
            constant_block.size_bytes = layout.push_constant_size;

            // The rewritten module is an intermediate: it exists so the
            // converter sees a module WGSL can express, and it is not shipped.
            const std::filesystem::path intermediate = module_path.parent_path() / (module_path.stem().string() + ".webgpu.tmp.spv");
            if (!WriteSpirv(intermediate, rewritten)) {
                error = "cannot write the intermediate module";
                return false;
            }

            const std::filesystem::path wgsl_path = module_path.parent_path() / (module_path.stem().string() + ".wgsl");
            // The two supported converters take the same information through
            // different command lines: naga infers the direction from the file
            // extensions, Tint asks for the output format explicitly.
            const bool is_tint = options.converter_executable.find("tint") != std::string::npos;
            const std::filesystem::path log_path =
                module_path.parent_path() / (module_path.stem().string() + ".convert.log");
            // The converter's own diagnostic is what makes a failure actionable,
            // so it is captured rather than discarded and quoted back in the
            // error message.
            const std::string redirect = " > " + Quote(log_path) + " 2>&1";
            const std::string convert = is_tint
                                            ? Quote(options.converter_executable) + " -f wgsl -o " + Quote(wgsl_path)
                                                  + " " + Quote(intermediate) + redirect
                                            : Quote(options.converter_executable) + " " + Quote(intermediate) + " "
                                                  + Quote(wgsl_path) + redirect;
            const bool converted = Run(convert) == 0 && std::filesystem::is_regular_file(wgsl_path);
            std::error_code remove_error;
            std::filesystem::remove(intermediate, remove_error);
            if (!converted) {
                error = "the converter did not produce WGSL: " + FirstLogLine(log_path);
                return false;
            }
            std::filesystem::remove(log_path, remove_error);

            if (options.validate) {
                // Tint validates an input by compiling it again; naga validates
                // when it is given no output file.
                const std::filesystem::path validate_output =
                    module_path.parent_path() / (module_path.stem().string() + ".validate.tmp.wgsl");
                const std::string validate_command =
                    is_tint ? Quote(options.converter_executable) + " -f wgsl -o " + Quote(validate_output) + " "
                                  + Quote(wgsl_path) + k_silence
                            : Quote(options.converter_executable) + " " + Quote(wgsl_path) + k_silence;
                const bool validated = Run(validate_command) == 0;
                std::filesystem::remove(validate_output, remove_error);
                if (!validated) {
                    error = "the WGSL the converter produced does not validate";
                    return false;
                }
            }

            const std::filesystem::path description_path =
                module_path.parent_path() / (module_path.stem().string() + ".iface.json");
            if (!WriteInterfaceDescription(
                    description_path,
                    ShaderNameOf(module_path),
                    StageOf(module_path),
                    "main",
                    layout,
                    constant_block
                )) {
                error = "cannot write the interface description";
                return false;
            }
            return true;
        }
    } // namespace

    std::string DefaultConverterExecutable() {
        if (const char *converter = std::getenv("SHADER_WGSL_CONVERTER");
            converter != nullptr && *converter != '\0') {
            return converter;
        }
        if (const char *tint = std::getenv("TINT"); tint != nullptr && *tint != '\0') {
            return tint;
        }
        // The reference compiler is the default: it is the one that expresses
        // the layouts this corpus uses.
        return "tint";
    }

    int BakeWgslTree(const WgslBakeOptions &options) {
        if (!std::filesystem::is_directory(options.spirv_directory)) {
            std::cerr << "shader_baker: not a directory: " << options.spirv_directory.string() << '\n';
            return 1;
        }

        std::vector<std::filesystem::path> modules;
        for (const std::filesystem::directory_entry &entry :
             std::filesystem::recursive_directory_iterator(options.spirv_directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".spv") {
                modules.push_back(entry.path());
            }
        }
        std::sort(modules.begin(), modules.end());

        size_t failures = 0;
        for (const std::filesystem::path &module_path : modules) {
            std::string error;
            if (BakeModule(options, module_path, error)) {
                std::cout << "  baked " << module_path.filename().string() << " -> .wgsl + .iface.json\n";
            } else {
                ++failures;
                std::cerr << "  FAILED " << module_path.string() << ": " << error << '\n';
            }
        }
        std::cout << "WGSL bake: " << (modules.size() - failures) << " of " << modules.size() << " module(s) baked\n";
        return static_cast<int>(failures);
    }
} // namespace Engine::ShaderBake





