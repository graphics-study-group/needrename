#include "Rhi/Pipeline/ShaderParameterLayout.h"

#include <stdexcept>

namespace Engine::Rhi {
    // The shape that has no reflection.
    //
    // SPLayout is the result of parsing SPIR-V: it records the binding table, the variable
    // offsets and the constant block. This backend never parses a module - the bake did, and its
    // answers arrive as a ShaderInterfaceDescription. So the class exists to keep the interface
    // intact and to report, once and clearly, that a caller asking to reflect has taken a path
    // this backend does not have.
    //
    // ShaderParameterLayout.cpp is not built here for the same reason: it links SPIRV-Cross.

    std::unordered_map<uint32_t, DescriptorSetLayout> SPLayout::GenerateAllLayoutBindings() const {
        // No interfaces were reflected, so there are no bindings to generate. An empty map is the
        // honest answer: a caller that gets one and builds a pipeline from it is asking a backend
        // without reflection to describe a shader.
        return {};
    }

    DescriptorSetLayout SPLayout::GenerateLayoutBindings(uint32_t, bool, bool) const {
        return {};
    }

    void SPLayout::Merge(SPLayout &&) {
        // Nothing reflected, nothing to merge.
    }

    void SPLayout::PlaceBufferVariable(
        std::vector<std::byte> &, const SPInterfaceStructuredBuffer &, const StructuredBuffer &
    ) const noexcept {
        // Nothing was reflected, so there are no variable offsets to place through.
    }

    SPLayout SPLayout::Reflect(const std::vector<uint32_t> &, bool) {
        throw std::logic_error(
            "SPLayout::Reflect: this backend has no reflection. Shaders arrive as WGSL with a "
            "ShaderInterfaceDescription produced by shader_baker."
        );
    }
} // namespace Engine::Rhi

