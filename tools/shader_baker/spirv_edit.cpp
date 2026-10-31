#include "spirv_edit.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Engine::ShaderBake {
    namespace {
        /// @brief SPIR-V opcodes this file reads or writes.
        enum Opcode : uint16_t {
            OpName = 5,
            OpMemberName = 6,
            OpTypeStruct = 30,
            OpTypePointer = 32,
            OpConstant = 43,
            OpConstantComposite = 44,
            OpVariable = 59,
            OpDecorate = 71,
            OpMemberDecorate = 72,
            OpDecorationGroup = 73,
            OpGroupDecorate = 74,
            OpGroupMemberDecorate = 75,
            OpDecorateId = 332,
        };

        /// @brief SPIR-V decorations this file reads or writes.
        enum Decoration : uint32_t {
            DecorationBlock = 2,
            DecorationNonReadable = 25,
            DecorationBinding = 33,
            DecorationDescriptorSet = 34,
        };

        /// @brief SPIR-V storage classes this file reads or writes.
        enum StorageClass : uint32_t {
            StorageClassUniform = 2,
            StorageClassPushConstant = 9,
        };

        /// @brief One instruction of the module, addressed in the word array.
        struct Instruction {
            size_t word_index{0};
            uint16_t opcode{0};
            uint16_t word_count{0};
        };

        uint16_t OpcodeOf(uint32_t word) {
            return static_cast<uint16_t>(word & 0xFFFFu);
        }

        uint16_t WordCountOf(uint32_t word) {
            return static_cast<uint16_t>(word >> 16);
        }

        uint32_t EncodeInstruction(uint16_t opcode, uint16_t word_count) {
            return (static_cast<uint32_t>(word_count) << 16) | opcode;
        }

        /// @brief Split a module into its instructions, without validating it.
        bool Parse(const std::vector<uint32_t> &spirv, std::vector<Instruction> &instructions, std::string &error) {
            if (spirv.size() < 5 || spirv[0] != 0x07230203u) {
                error = "not a SPIR-V module (bad magic number)";
                return false;
            }
            // A module's word count is `size - 5` with the header counted as
            // five words; the first instruction starts right after it.
            size_t offset = 5;
            while (offset < spirv.size()) {
                const uint16_t word_count = WordCountOf(spirv[offset]);
                if (word_count == 0 || offset + word_count > spirv.size()) {
                    error = "truncated instruction stream";
                    return false;
                }
                instructions.push_back(Instruction{offset, OpcodeOf(spirv[offset]), word_count});
                offset += word_count;
            }
            return true;
        }

        /**
         * @brief Find the first index after the module's annotation section.
         *
         * Decorations have to live in the annotation section, which sits before
         * the type, constant and global-variable declarations. Inserting a
         * decoration right after the last existing annotation keeps the module
         * in the layout the specification requires.
         */
        size_t AnnotationInsertIndex(const std::vector<Instruction> &instructions) {
            size_t index = 0;
            for (size_t candidate = 0; candidate < instructions.size(); ++candidate) {
                switch (instructions[candidate].opcode) {
                case OpName:
                case OpMemberName:
                case OpDecorate:
                case OpMemberDecorate:
                case OpDecorationGroup:
                case OpGroupDecorate:
                case OpGroupMemberDecorate:
                case OpDecorateId:
                    index = candidate + 1;
                    break;
                default:
                    break;
                }
            }
            return index;
        }

        /// @brief Whether a struct type already carries the `Block` decoration.
        bool HasBlockDecoration(const std::vector<uint32_t> &spirv, const std::vector<Instruction> &instructions, uint32_t type_id) {
            for (const Instruction &instruction : instructions) {
                if (instruction.opcode != OpDecorate) {
                    continue;
                }
                const size_t operands = instruction.word_index + 1;
                if (operands + 2 >= spirv.size() || spirv[operands] != type_id) {
                    continue;
                }
                if (spirv[operands + 1] == DecorationBlock) {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    std::vector<uint32_t> FreezePushConstantsToUniform(
        const std::vector<uint32_t> &spirv,
        uint32_t reserved_set,
        uint32_t reserved_binding,
        uint32_t /*block_size_bytes*/,
        std::string &error
    ) {
        std::vector<Instruction> instructions;
        if (!Parse(spirv, instructions, error)) {
            return {};
        }

        // The push-constant variable, its pointer type, and the struct behind it
        // are the three things the rewrite needs to know.
        size_t variable_index = instructions.size();
        uint32_t variable_id = 0;
        uint32_t pointer_type_id = 0;
        for (size_t index = 0; index < instructions.size(); ++index) {
            const Instruction &instruction = instructions[index];
            if (instruction.opcode != OpVariable || instruction.word_count < 4) {
                continue;
            }
            const size_t operands = instruction.word_index + 1;
            if (spirv[operands + 2] == StorageClassPushConstant) {
                variable_index = index;
                pointer_type_id = spirv[operands];
                variable_id = spirv[operands + 1];
                break;
            }
        }
        if (variable_index == instructions.size()) {
            error.clear();
            return spirv;
        }
        if (variable_id == 0) {
            error = "the push-constant variable has no result id";
            return {};
        }

        // The struct a push constant points at is the block type; a uniform
        // buffer needs that same struct decorated as a Block.
        uint32_t struct_type_id = 0;
        for (const Instruction &instruction : instructions) {
            if (instruction.opcode != OpTypePointer || instruction.word_count < 4) {
                continue;
            }
            const size_t operands = instruction.word_index + 1;
            if (spirv[operands] == pointer_type_id) {
                struct_type_id = spirv[operands + 2];
                break;
            }
        }

        std::vector<uint32_t> rewritten = spirv;
        {
            const size_t operands = rewritten.empty() ? 0 : instructions[variable_index].word_index + 1;
            rewritten[operands + 2] = StorageClassUniform;
        }

        std::vector<uint32_t> decorations;
        const auto append_decoration = [&decorations](uint32_t target, uint32_t decoration, uint32_t value) {
            decorations.push_back(EncodeInstruction(OpDecorate, 4));
            decorations.push_back(target);
            decorations.push_back(decoration);
            decorations.push_back(value);
        };
        append_decoration(variable_id, DecorationDescriptorSet, reserved_set);
        append_decoration(variable_id, DecorationBinding, reserved_binding);
        const bool needs_block = struct_type_id != 0 && !HasBlockDecoration(rewritten, instructions, struct_type_id);
        if (needs_block) {
            decorations.push_back(EncodeInstruction(OpDecorate, 3));
            decorations.push_back(struct_type_id);
            decorations.push_back(DecorationBlock);
        }

        const size_t insert_after = AnnotationInsertIndex(instructions);
        size_t insert_word = rewritten.size();
        if (insert_after < instructions.size()) {
            insert_word = instructions[insert_after].word_index;
        }
        rewritten.insert(rewritten.begin() + static_cast<std::ptrdiff_t>(insert_word), decorations.begin(), decorations.end());
        error.clear();
        return rewritten;
    }

    std::vector<uint32_t> StripWriteOnlyDecorations(const std::vector<uint32_t> &spirv, std::string &error) {
        std::vector<Instruction> instructions;
        if (!Parse(spirv, instructions, error)) {
            return {};
        }

        std::vector<uint32_t> rewritten;
        rewritten.reserve(spirv.size());
        rewritten.insert(rewritten.end(), spirv.begin(), spirv.begin() + 5);

        size_t removed = 0;
        for (const Instruction &instruction : instructions) {
            const bool is_variable_decoration = instruction.opcode == OpDecorate;
            const bool is_member_decoration = instruction.opcode == OpMemberDecorate;
            if ((is_variable_decoration && instruction.word_count >= 3)
                || (is_member_decoration && instruction.word_count >= 4)) {
                // `OpDecorate %var NonReadable` puts the decoration in the second
                // operand slot; `OpMemberDecorate %struct %member NonReadable`
                // puts it in the third, after the member index.
                const size_t decoration_operand = is_variable_decoration ? 1 : 2;
                if (spirv[instruction.word_index + 1 + decoration_operand] == DecorationNonReadable) {
                    ++removed;
                    continue;
                }
            }
            rewritten.insert(
                rewritten.end(),
                spirv.begin() + static_cast<std::ptrdiff_t>(instruction.word_index),
                spirv.begin() + static_cast<std::ptrdiff_t>(instruction.word_index + instruction.word_count)
            );
        }
        (void)removed;
        error.clear();
        return rewritten;
    }

    std::string DemoteSpecializationConstants(const std::string &glsl, bool &demoted_any) {
        demoted_any = false;
        std::string result;
        result.reserve(glsl.size());

        size_t line_start = 0;
        while (line_start <= glsl.size()) {
            size_t line_end = glsl.find('\n', line_start);
            if (line_end == std::string::npos) {
                line_end = glsl.size();
            }
            const std::string line = glsl.substr(line_start, line_end - line_start);

            // A specialization-constant declaration is a `layout(constant_id =
            // N)` qualifier on a `const` declaration; dropping the layout and
            // keeping the `const` is exactly what the WGSL variant needs.
            const size_t constant_id = line.find("constant_id");
            if (constant_id != std::string::npos) {
                const size_t layout_open = line.rfind("layout", constant_id);
                const size_t layout_close =
                    layout_open == std::string::npos ? std::string::npos : line.find(')', constant_id);
                if (layout_open != std::string::npos && layout_close != std::string::npos) {
                    result.append(line.substr(0, layout_open));
                    result.append(line.substr(layout_close + 1));
                    demoted_any = true;
                    if (line_end < glsl.size()) {
                        result.push_back('\n');
                    }
                    line_start = line_end + 1;
                    continue;
                }
            }

            result.append(line);
            if (line_end < glsl.size()) {
                result.push_back('\n');
            }
            line_start = line_end + 1;
            if (line_end == glsl.size()) {
                break;
            }
        }
        return result;
    }
} // namespace Engine::ShaderBake
