#ifndef ENGINE_SHADERBAKER_SPIRV_EDIT_INCLUDED
#define ENGINE_SHADERBAKER_SPIRV_EDIT_INCLUDED

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::ShaderBake {
    /**
     * @brief What a rewritten module carries about its constant block.
     *
     * The bake turns a shader's push-constant block into a uniform buffer,
     * because WGSL has no push-constant address space and the WebGPU API has no
     * way to set one. The block's location and size are recorded here so they
     * can be written into the shader's interface description, which is what the
     * backend binds against.
     */
    struct ConstantBlockLayout {
        /// @brief Whether the module declared a push-constant block at all.
        bool has_block{false};
        /// @brief Descriptor set the block was rewritten into.
        uint32_t set{0};
        /// @brief Binding the block was rewritten into.
        uint32_t binding{0};
        /// @brief Size in bytes of the block the shader reads.
        uint32_t size_bytes{0};
    };

    /**
     * @brief Rewrite a module's push-constant block into a uniform buffer.
     *
     * The transformation is:
     *
     * - the `PushConstant` variable's storage class becomes `Uniform`;
     * - the variable gains `DescriptorSet`/`Binding` decorations naming the
     *   reserved location, and its struct type is guaranteed to be
     *   `Block`-decorated, which a uniform buffer requires;
     * - nothing else in the module changes, so every offset the shader already
     *   had still holds.
     *
     * A module with no push-constant block is returned unchanged, with
     * `has_block` false.
     *
     * @param spirv The module to rewrite, as SPIR-V words.
     * @param reserved_set Set to place the block in; the caller picks a set no
     * shader interface uses.
     * @param reserved_binding Binding to place the block at within that set.
     * @param block_size_bytes The block's size, from the reflection of the
     * original module; recorded in the returned layout.
     * @param error Receives a description of what went wrong on failure.
     * @return The rewritten module, or an empty vector on failure.
     */
    std::vector<uint32_t> FreezePushConstantsToUniform(
        const std::vector<uint32_t> &spirv,
        uint32_t reserved_set,
        uint32_t reserved_binding,
        uint32_t block_size_bytes,
        std::string &error
    );

    /**
     * @brief Drop the `NonReadable` decorations from a module.
     *
     * GLSL's `writeonly buffer` becomes SPIR-V's `NonReadable` decoration on the
     * variable, and WGSL has no write-only storage buffer: a storage buffer is
     * `read` or `read_write`. Dropping the decoration widens the access the
     * module declares, which is always safe — the qualifier constrains the
     * driver, not the shader's behaviour.
     *
     * @param spirv The module to rewrite, as SPIR-V words.
     * @param error Receives a description of what went wrong on failure.
     * @return The rewritten module, or an empty vector on failure.
     */
    std::vector<uint32_t> StripWriteOnlyDecorations(const std::vector<uint32_t> &spirv, std::string &error);

    /**
     * @brief Drop a module's non-semantic debug information.
     *
     * A debug build embeds `NonSemantic.Shader.DebugInfo.100` ext-instructions,
     * which are informational only. The reference WGSL compiler validates them
     * strictly enough to reject some compiler-generated ones, and WGSL has no
     * debug-info construct to carry them into, so the WGSL variant is built from
     * a module without them.
     *
     * Only that instruction set is removed: `GLSL.std.450` and the other
     * ext-instruction sets stay, because they carry meaning.
     *
     * @param spirv The module to rewrite, as SPIR-V words.
     * @param error Receives a description of what went wrong on failure.
     * @return The rewritten module, or an empty vector on failure.
     */
    std::vector<uint32_t> StripNonSemanticDebugInfo(const std::vector<uint32_t> &spirv, std::string &error);

    /**
     * @brief Demote specialization-constant declarations in GLSL source to plain constants.
     *
     * A shader that declares `layout(constant_id = N) const T name = value;`
     * makes glslang emit `OpSpecConstantOp` for every comparison against that
     * constant, which the SPIR-V-to-WGSL converter rejects; WGSL has no
     * specialization constants anyway, so the WGSL variant of a shader is built
     * from a source where the declaration is a plain `const` and the value is
     * fixed at bake time.
     *
     * @param glsl The shader source.
     * @return The source with every specialization-constant declaration demoted,
     * and whether the source declared any.
     */
    std::string DemoteSpecializationConstants(const std::string &glsl, bool &demoted_any);
} // namespace Engine::ShaderBake

#endif // ENGINE_SHADERBAKER_SPIRV_EDIT_INCLUDED
