#ifndef ENGINE_SHADERBAKER_WGSL_BAKE_INCLUDED
#define ENGINE_SHADERBAKER_WGSL_BAKE_INCLUDED

#include <filesystem>
#include <string>
#include <vector>

namespace Engine::ShaderBake {
    /// @brief What to bake, and with which converter.
    struct WgslBakeOptions {
        /// @brief Directory searched recursively for `.spv` modules.
        std::filesystem::path spirv_directory{};
        /// @brief SPIR-V to WGSL converter to invoke; a host tool, never linked.
        std::string converter_executable{"naga"};
        /// @brief Whether to ask the converter to validate every WGSL it produced.
        bool validate{true};
    };

    /**
     * @brief Bake the WGSL variant and the interface description for every module in a tree.
     *
     * For each `.spv` module this writes, next to it, the shader's WGSL variant
     * and its interface description. The WebGPU backend loads those two and
     * reflects nothing at runtime, which is what keeps SPIRV-Cross out of the
     * web build.
     *
     * The description is the single source of CPU-side placement truth: it
     * records the binding table, the constant block's location and size, and
     * (once the combined-sampler split lands) the map from a combined binding to
     * the texture and sampler bindings that replace it.
     *
     * @param options What to bake and how.
     * @return The number of modules that failed to bake; zero means success.
     */
    int BakeWgslTree(const WgslBakeOptions &options);

    /// @brief Get the default converter path: `$NAGA` when set, otherwise `naga`.
    std::string DefaultConverterExecutable();

    /**
     * @brief Convert one SPIR-V module to WGSL text, with the rewrites WGSL requires.
     *
     * The tree bake and the shader-archive bake differ only in where the module comes from and
     * where the text goes; the rewrites - stripped write-only qualifiers, stripped non-semantic
     * debug info, the push-constant block moved to a uniform buffer - are the same for both, so
     * they live here once.
     *
     * @param spirv The module, as read from a `.spv` file.
     * @param converter_executable SPIR-V to WGSL converter to invoke.
     * @param wgsl Receives the converted text.
     * @param error Receives a diagnostic when the conversion fails.
     * @return Whether WGSL was produced.
     */
    bool ConvertSpirvToWgsl(
        const std::vector<uint32_t> &spirv,
        const std::string &converter_executable,
        std::string &wgsl,
        std::string &error,
        const std::filesystem::path &scratch_directory
    );

    /**
     * @brief Write the interface description for one SPIR-V module to a stream.
     *
     * @param spirv The module; reflected here.
     * @param shader_name The name to record.
     * @param stage The stage to record.
     * @param wgsl The converted text, which decides each storage binding's access.
     * @param json Receives the description.
     * @return Whether reflection succeeded.
     */
    bool WriteInterfaceDescriptionForModule(
        const std::vector<uint32_t> &spirv,
        const std::string &shader_name,
        const std::string &stage,
        const std::string &wgsl,
        std::string &json
    );
} // namespace Engine::ShaderBake

#endif // ENGINE_SHADERBAKER_WGSL_BAKE_INCLUDED
