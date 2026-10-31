#ifndef ENGINE_SHADERBAKER_WGSL_BAKE_INCLUDED
#define ENGINE_SHADERBAKER_WGSL_BAKE_INCLUDED

#include <filesystem>
#include <string>

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
} // namespace Engine::ShaderBake

#endif // ENGINE_SHADERBAKER_WGSL_BAKE_INCLUDED
