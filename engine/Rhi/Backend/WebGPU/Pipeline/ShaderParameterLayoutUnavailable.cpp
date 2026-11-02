#include "Rhi/Pipeline/ShaderParameterLayout.h"

#include <stdexcept>

namespace Engine::Rhi {
    // The shape that has no reflection.
    //
    // A layout is filled by parsing SPIR-V, which this backend never does: the bake parsed it and
    // its answers arrive as a ShaderInterfaceDescription. Everything a layout *does* - merging a
    // shader's stages, restating itself as descriptor set layouts, placing a variable into a block
    // - lives in `SPLayoutCore.cpp` and is compiled here too. Only the parsing is missing.

    SPLayout SPLayout::Reflect(const std::vector<uint32_t> &, bool) {
        throw std::logic_error(
            "SPLayout::Reflect: this backend has no reflection. Shaders arrive as WGSL with a "
            "ShaderInterfaceDescription produced by shader_baker."
        );
    }
} // namespace Engine::Rhi