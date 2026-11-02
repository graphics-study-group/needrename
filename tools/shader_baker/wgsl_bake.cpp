#include "wgsl_bake.h"

#include "Rhi/Buffer/StructuredBufferPlacer.h"

#include <cstdio>

#include "spirv_edit.h"

#include <Rhi/Pipeline/ShaderInterface.h>
#include <Rhi/Pipeline/ShaderParameterLayout.h>

#include <algorithm>
#include <map>
#include <regex>
#include <utility>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <regex>
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

        /**
         * @brief Pin every combined sampler's split slot in the converter's output.
         *
         * A converter is free to choose where the sampler of a combined binding lands, and the
         * reference one chooses the binding after the texture only when that slot is free - it
         * has been observed to place one past the set's highest binding instead. The runtime
         * builds its layouts from `SplitSamplerBinding`, so the bake has to state the same
         * answer rather than let the converter pick: `--sampler-mapping` is how.
         *
         * The converter reads a single digit for the destination binding, so a slot above nine
         * is not expressible and the bake says so instead of producing a shader whose sampler
         * nothing binds.
         */
        bool SamplerMappingArgument(
            const Engine::Rhi::SPLayout &layout, std::string &argument, std::string &error
        ) {
            using namespace Engine::Rhi;
            std::string mapping;
            for (const auto &interface : layout.interfaces) {
                const auto *image = dynamic_cast<const SPInterfaceOpaqueImage *>(interface.get());
                if (image == nullptr) {
                    continue;
                }
                if (!image->flags.Test(SPInterfaceOpaqueImage::ImageFlagBits::HasSampler)) {
                    continue;
                }
                const uint32_t slot = SplitSamplerBinding(interface->layout_binding);
                if (slot > 9u) {
                    error = "the sampler of '" + interface->name + "' would land at binding "
                            + std::to_string(slot)
                            + ", which the converter cannot express; renumber the shader so the"
                              " slot after the texture is free and below ten.";
                    return false;
                }
                if (!mapping.empty()) {
                    mapping += " ";
                }
                mapping += std::to_string(interface->layout_set) + "," + std::to_string(interface->layout_binding)
                           + ":" + std::to_string(interface->layout_set) + "," + std::to_string(slot);
            }
            argument = mapping.empty() ? std::string{} : " --sampler-mapping \"" + mapping + "\"";
            return true;
        }

        /// @brief The shell's way of discarding a program's output.
        constexpr const char *k_silence =
#ifdef _WIN32
            " > NUL 2>&1";
#else
            " > /dev/null 2>&1";
#endif

        /**
         * @brief Extra arguments the reference converter needs for this corpus.
         *
         * `--allow-non-uniform-derivatives` turns off the WGSL uniformity diagnostic for implicit
         * derivatives, which is the one a shadow lookup trips: `textureSampleCompare` derives its
         * LOD, and the shadow function is entered from a cascade branch whose condition is a
         * per-fragment distance. The branch is uniform within a draw in practice (a cascade is
         * selected for a whole surface region), and the directive the flag inserts is WGSL's own
         * way of stating that the diagnostic is not being relied upon - it is not a validation
         * bypass.
         */
        constexpr const char *k_tint_flags = " --allow-non-uniform-derivatives";

        /// @brief Lower-case copy of a string, for case-insensitive matching.
        std::string LowerCaseCopy(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return text;
        }

        /// @brief Get the first diagnostic line of a log file, for quoting back in an error.
        std::string FirstLogLine(const std::filesystem::path &log_path) {
            std::ifstream log_file(log_path);
            if (!log_file.is_open()) {
                return "no diagnostic was produced";
            }
            std::string first_error;
            std::string first_non_empty;
            std::string line;
            while (std::getline(log_file, line)) {
                if (line.empty() || line.find_first_not_of(" \t\r") == std::string::npos) {
                    continue;
                }
                if (first_non_empty.empty()) {
                    first_non_empty = line;
                }
                const std::string lowered = LowerCaseCopy(line);
                if (first_error.empty() && lowered.find("error") != std::string::npos) {
                    first_error = line;
                }
            }
            if (!first_error.empty()) {
                return first_error;
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
         *
         * A combined image sampler has no single binding on this interface: the converter
         * declares the texture at the shader's binding and its sampler at the next one, so the
         * description names both. A shader whose next binding is already taken fails the
         * agreement check below, which is where that collision should be caught.
         */
        /// @brief The access each storage binding declares in the converted WGSL.
        ///
        /// The converter infers read versus read-write from how the module uses a binding, which
        /// is not the same thing as the SPIR-V decoration: a GLSL buffer declared without
        /// `readonly` but only read becomes `var<storage, read>`. The layout has to match what the
        /// runtime compiles, so the description takes the access from the WGSL text.
        std::map<std::pair<uint32_t, uint32_t>, bool> StorageAccessFromWgsl(const std::string &wgsl) {
            std::map<std::pair<uint32_t, uint32_t>, bool> access;
            std::istringstream lines{wgsl};
            std::string line;
            std::regex binding_pattern(
                R"(@group\((\d+)u\)\s*@binding\((\d+)u\)\s*var<storage,\s*(read|read_write)>)"
            );
            std::regex storage_texture_pattern(
                R"(@group\((\d+)u\)\s*@binding\((\d+)u\)\s*var\s+\w+\s*:\s*texture_storage_\w+<[^,]+,\s*(read|write|read_write)>)"
            );
            std::smatch match;
            while (std::getline(lines, line)) {
                if (std::regex_search(line, match, binding_pattern)) {
                    access[{static_cast<uint32_t>(std::stoul(match[1])), static_cast<uint32_t>(std::stoul(match[2]))}] =
                        match[3] == "read";
                } else if (std::regex_search(line, match, storage_texture_pattern)) {
                    access[{static_cast<uint32_t>(std::stoul(match[1])), static_cast<uint32_t>(std::stoul(match[2]))}] =
                        match[3] == "read";
                }
            }
            return access;
        }

        bool WriteInterfaceDescriptionJson(
            std::ostringstream &json,
            const std::string &shader_name,
            const std::string &stage,
            const std::string &entry_point,
            const Engine::Rhi::SPLayout &layout,
            const ConstantBlockLayout &constant_block,
            const std::map<std::pair<uint32_t, uint32_t>, bool> &access_from_wgsl = {}
        ) {
            struct Entry {
                uint32_t set;
                uint32_t binding;
                const char *kind;
                std::string name;
                uint32_t storage_format;
                bool storage_read_only;
                std::vector<Engine::Rhi::StructuredBufferPlacer::Entry> members{};
                uint32_t block_size{0};
                uint32_t sample_type{0};
                uint32_t view_dimension{1};
            };
            std::vector<Entry> entries;
            for (const auto &interface : layout.interfaces) {
                const char *kind = DescribeKind(*interface);
                uint32_t storage_format = 0;
                bool storage_read_only = false;
                if (const auto *storage = dynamic_cast<const Engine::Rhi::SPInterfaceOpaqueStorageImage *>(interface.get())) {
                    // A storage image's format and access are part of its declaration, and the
                    // runtime's layout has to state both: a binding whose format disagrees with
                    // the shader's is a pipeline that does not build.
                    storage_format = storage->format;
                    storage_read_only = storage->read_only;
                }
                entries.push_back(
                    Entry{
                        interface->layout_set,
                        interface->layout_binding,
                        kind,
                        interface->name,
                        storage_format,
                        storage_read_only
                    }
                );
                if (const auto *image = dynamic_cast<const Engine::Rhi::SPInterfaceOpaqueImage *>(interface.get());
                    image != nullptr) {
                    using Flags = Engine::Rhi::SPInterfaceOpaqueImage::ImageFlagBits;
                    entries.back().sample_type = image->flags.Test(Flags::Depth) ? 1u : 0u;
                    if (image->flags.Test(Flags::d1D)) {
                        entries.back().view_dimension = 0u;
                    } else if (image->flags.Test(Flags::d3D)) {
                        entries.back().view_dimension = 2u;
                    } else if (image->flags.Test(Flags::CubeMap)) {
                        entries.back().view_dimension = image->flags.Test(Flags::Arrayed) ? 5u : 3u;
                    } else if (image->flags.Test(Flags::Arrayed)) {
                        entries.back().view_dimension = 4u;
                    } else {
                        entries.back().view_dimension = 1u;
                    }
                }
                if (const auto *structured =
                        dynamic_cast<const Engine::Rhi::SPInterfaceStructuredBuffer *>(interface.get());
                    structured != nullptr && structured->buffer_placer != nullptr) {
                    std::fprintf(
                        stderr,
                        "bake: block '%s' set %u binding %u has %zu member(s), %zu bytes\n",
                        interface->name.c_str(),
                        interface->layout_set,
                        interface->layout_binding,
                        structured->buffer_placer->Enumerate().size(),
                        structured->buffer_placer->GetBlockSize()
                    );
                    // The block's variables, so a runtime without reflection can place them.
                    entries.back().members = structured->buffer_placer->Enumerate();
                    entries.back().block_size = static_cast<uint32_t>(structured->buffer_placer->GetBlockSize());
                }
                const auto override_entry =
                    access_from_wgsl.find({interface->layout_set, interface->layout_binding});
                if (override_entry != access_from_wgsl.end()) {
                    entries.back().storage_read_only = override_entry->second;
                }
            }

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
            for (size_t index = 0; index < entries.size(); ++index) {
                const Entry &entry = entries[index];
                json << "    { \"set\": " << entry.set << ", \"binding\": " << entry.binding << ", \"kind\": \""
                     << entry.kind << "\", \"name\": \"" << entry.name << "\"";
                if (std::string_view{entry.kind} == "combined-image-sampler"
                    || std::string_view{entry.kind} == "sampled-image") {
                    // How the binding is declared: a skybox is a cube, a shadow map an array, and a
                    // layout that says otherwise is a pipeline the device refuses.
                    json << ", \"sample_type\": " << static_cast<uint32_t>(entry.sample_type)
                         << ", \"view_dimension\": " << static_cast<uint32_t>(entry.view_dimension);
                }
                if (std::string_view{entry.kind} == "storage-buffer") {
                    json << ", \"read_only\": " << (entry.storage_read_only ? "true" : "false");
                }
                if (!entry.members.empty()) {
                    // The variables a material places by name. A runtime with no reflection library
                    // rebuilds the block from these offsets instead of reading the module.
                    json << ", \"block_size\": " << entry.block_size << ", \"members\": [";
                    for (size_t member_index = 0; member_index < entry.members.size(); member_index++) {
                        const auto &member = entry.members[member_index];
                        if (member_index != 0) {
                            json << ", ";
                        }
                        json << "{ \"name\": \"" << member.name << "\", \"offset\": " << member.offset
                             << ", \"size\": " << member.size << ", \"kind\": "
                             << static_cast<uint32_t>(member.kind) << " }";
                    }
                    json << "]";
                }
                if (std::string_view{entry.kind} == "storage-image") {
                    json << ", \"format\": " << entry.storage_format
                         << ", \"read_only\": " << (entry.storage_read_only ? "true" : "false");
                }
                if (std::string_view{entry.kind} == "combined-image-sampler") {
                    json << ", \"sampler_binding\": " << Engine::Rhi::SplitSamplerBinding(entry.binding);
                }
                json << " }";
                json << (index + 1 == entries.size() ? "\n" : ",\n");
            }
            json << "  ]\n";
            json << "}\n";
            return true;
        }

        bool WriteInterfaceDescription(
            const std::filesystem::path &path,
            const std::string &shader_name,
            const std::string &stage,
            const std::string &entry_point,
            const Engine::Rhi::SPLayout &layout,
            const ConstantBlockLayout &constant_block,
            const std::map<std::pair<uint32_t, uint32_t>, bool> &access_from_wgsl = {}
        ) {
            std::ostringstream json;
            if (!WriteInterfaceDescriptionJson(
                    json, shader_name, stage, entry_point, layout, constant_block, access_from_wgsl
                )) {
                return false;
            }

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

            // Where a push-constant block lands. It is the whole block, so the number is a
            // convention rather than a computation: the engine's material pipelines use groups
            // 0, 1 and 2 and the standard interface allows four, which leaves 3 for the block
            // and makes it the same group for every shader - a per-shader choice here would
            // give a vertex and a fragment stage different groups for the same pipeline.
            constexpr uint32_t k_constant_set = 3;
            constexpr uint32_t k_reserved_binding = 0;
            const uint32_t reserved_set = k_constant_set;

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
            std::string sampler_mapping;
            if (is_tint && !SamplerMappingArgument(layout, sampler_mapping, error)) {
                return false;
            }
            const std::filesystem::path log_path =
                module_path.parent_path() / (module_path.stem().string() + ".convert.log");
            // The converter's own diagnostic is what makes a failure actionable,
            // so it is captured rather than discarded and quoted back in the
            // error message.
            const std::string redirect = " > " + Quote(log_path) + " 2>&1";
            const std::string convert = is_tint
                                            ? Quote(options.converter_executable) + " -f wgsl -o " + Quote(wgsl_path)
                                                  + " " + Quote(intermediate) + k_tint_flags + sampler_mapping
                                                  + redirect
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

            // The description is the source of truth the runtime binds against, so the WGSL and
            // the description must agree on which slots exist. A mismatch here is a bake bug that
            // would otherwise surface as a validation error at dispatch, far from its cause.
            {
                std::ifstream wgsl_file(wgsl_path);
                std::stringstream wgsl_text;
                wgsl_text << wgsl_file.rdbuf();

                std::set<std::pair<uint32_t, uint32_t>> declared;
                const std::regex slot(R"(@group\((\d+)u?\)\s*@binding\((\d+)u?\))");
                const std::string text = wgsl_text.str();
                for (auto it = std::sregex_iterator(text.begin(), text.end(), slot);
                     it != std::sregex_iterator();
                     ++it) {
                    declared.emplace(
                        static_cast<uint32_t>(std::stoul((*it)[1].str())),
                        static_cast<uint32_t>(std::stoul((*it)[2].str()))
                    );
                }

                std::set<std::pair<uint32_t, uint32_t>> described;
                for (const auto &interface : layout.interfaces) {
                    described.emplace(interface->layout_set, interface->layout_binding);
                    if (dynamic_cast<const Engine::Rhi::SPInterfaceOpaqueImage *>(interface.get()) != nullptr) {
                        // The converter declares the sampler of a combined binding as its own
                        // binding, one slot after the texture, so the WGSL has a slot the
                        // description does not list as an interface.
                        described.emplace(
                            interface->layout_set, Engine::Rhi::SplitSamplerBinding(interface->layout_binding)
                        );
                    }
                }
                if (constant_block.has_block) {
                    described.emplace(constant_block.set, constant_block.binding);
                }

                if (declared != described) {
                    std::ostringstream message;
                    message << "the baked WGSL and the interface description disagree";
                    for (const auto &[set, binding] : declared) {
                        if (described.count({set, binding}) == 0) {
                            message << "; WGSL declares set " << set << " binding " << binding
                                    << " that the description does not";
                        }
                    }
                    for (const auto &[set, binding] : described) {
                        if (declared.count({set, binding}) == 0) {
                            message << "; the description names set " << set << " binding " << binding
                                    << " that the WGSL does not declare";
                        }
                    }
                    error = message.str();
                    return false;
                }
            }

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
            std::string converted_text;
            {
                std::ifstream converted_file(wgsl_path);
                std::ostringstream converted_stream;
                converted_stream << converted_file.rdbuf();
                converted_text = converted_stream.str();
            }
            if (!WriteInterfaceDescription(
                    description_path,
                    ShaderNameOf(module_path),
                    StageOf(module_path),
                    "main",
                    layout,
                    constant_block,
                    StorageAccessFromWgsl(converted_text)
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

    bool ConvertSpirvToWgsl(
        const std::vector<uint32_t> &spirv,
        const std::string &converter_executable,
        std::string &wgsl,
        std::string &error,
        const std::filesystem::path &scratch_directory
    ) {
        std::vector<uint32_t> widened = StripWriteOnlyDecorations(spirv, error);
        if (widened.empty()) {
            return false;
        }
        std::vector<uint32_t> undebugged = StripNonSemanticDebugInfo(widened, error);
        if (undebugged.empty()) {
            return false;
        }
        uint32_t push_constant_size = 0;
        std::string sampler_mapping;
        {
            // The rewrite needs the block's size, which is the one thing the caller of this
            // helper does not have; reflecting here is cheap and keeps the signature honest.
            const Engine::Rhi::SPLayout layout = Engine::Rhi::SPLayout::Reflect(spirv, false);
            push_constant_size = layout.push_constant_size;
            if (converter_executable.find("tint") != std::string::npos
                && !SamplerMappingArgument(layout, sampler_mapping, error)) {
                return false;
            }
        }
        // The same set the tree bake uses: see k_constant_set in BakeModule.
        const std::vector<uint32_t> rewritten =
            FreezePushConstantsToUniform(undebugged, 3u, 0u, push_constant_size, error);
        if (rewritten.empty()) {
            return false;
        }

        std::error_code remove_error;
        // The scratch files go next to the module rather than in the system temporary
        // directory: a build step may run in an environment that refuses writes there, and the
        // module's own directory is known to be writable because the bake writes beside it.
        const std::filesystem::path scratch =
            scratch_directory / ("needrename_wgsl_" + std::to_string(std::rand()));
        const std::filesystem::path intermediate = scratch.string() + ".spv";
        const std::filesystem::path output = scratch.string() + ".wgsl";
        const std::filesystem::path log_path = scratch.string() + ".log";
        if (!WriteSpirv(intermediate, rewritten)) {
            error = "cannot write the intermediate module: " + intermediate.string();
            return false;
        }

        const bool is_tint = converter_executable.find("tint") != std::string::npos;
        const std::string redirect = " > " + Quote(log_path) + " 2>&1";
        const std::string command = is_tint
                                        ? Quote(converter_executable) + " -f wgsl -o " + Quote(output) + " "
                                              + Quote(intermediate) + k_tint_flags + sampler_mapping + redirect
                                        : Quote(converter_executable) + " " + Quote(intermediate) + " "
                                              + Quote(output) + redirect;
        const bool converted = Run(command) == 0 && std::filesystem::is_regular_file(output);
        std::filesystem::remove(intermediate, remove_error);
        if (!converted) {
            error = "the converter did not produce WGSL: " + FirstLogLine(log_path);
            std::filesystem::remove(log_path, remove_error);
            return false;
        }
        std::filesystem::remove(log_path, remove_error);

        std::ifstream wgsl_file(output);
        std::stringstream wgsl_text;
        wgsl_text << wgsl_file.rdbuf();
        wgsl = wgsl_text.str();
        std::filesystem::remove(output, remove_error);
        if (wgsl.empty()) {
            error = "the converter wrote an empty module";
            return false;
        }
        return true;
    }

    bool WriteInterfaceDescriptionForModule(
        const std::vector<uint32_t> &spirv,
        const std::string &shader_name,
        const std::string &stage,
        const std::string &wgsl,
        std::string &json
    ) {
        const Engine::Rhi::SPLayout layout = Engine::Rhi::SPLayout::Reflect(spirv, false);
        ConstantBlockLayout constant_block{};
        constant_block.has_block = layout.push_constant_size != 0;
        constant_block.set = 3;
        constant_block.binding = 0;
        constant_block.size_bytes = layout.push_constant_size;
        std::ostringstream out;
        if (!WriteInterfaceDescriptionJson(
                out, shader_name, stage, "main", layout, constant_block, StorageAccessFromWgsl(wgsl)
            )) {
            return false;
        }
        json = out.str();
        return true;
    }

    int BakeWgslTree(const WgslBakeOptions &options) {        if (!std::filesystem::is_directory(options.spirv_directory)) {
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






