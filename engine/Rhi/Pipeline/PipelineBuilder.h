#ifndef ENGINE_RHI_PIPELINE_BUILDER_INCLUDED
#define ENGINE_RHI_PIPELINE_BUILDER_INCLUDED

#include "Rhi/Pipeline/PipelineDescription.h"
#include "Rhi/RhiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::Rhi {

    /// @brief Which stage of a pipeline a shader runs as.
    ///
    /// A stage belongs to the build, not to the shader: the same compiled code can
    /// be used as a vertex shader in one pipeline and a fragment shader in another,
    /// so the two are separate facts.
    enum class ShaderStage : uint8_t {
        /// @brief Produces clip-space positions.
        Vertex,
        /// @brief Produces fragment colour.
        Fragment,
        /// @brief A compute dispatch.
        Compute,
        /// @brief Produces primitives.
        Geometry,
        /// @brief Runs before primitive assembly.
        TessellationControl,
        /// @brief Runs after primitive assembly.
        TessellationEvaluation
    };

    /// @brief One shader a pipeline is built from, and the stage it runs as.
    ///
    /// The module and the stage are separate facts: the same code can be used by
    /// different stages in different pipelines, so a stage belongs to the build
    /// rather than to the module.
    struct GraphicsPipelineStage {
        /// @brief The compiled shader.
        ShaderModuleHandle module{};
        /// @brief Which stage it runs as.
        ShaderStage stage{ShaderStage::Vertex};
        /// @brief The function to start at.
        std::string entry_point{"main"};
    };

    /**
     * @brief Where one specialization constant lives in the data block.
     *
     * A specialization constant is a value baked into the pipeline at creation
     * rather than bound at draw time, so building a pipeline needs both the values
     * and where each one sits.
     */
    struct SpecializationConstant {
        /// @brief The constant's id in the shader.
        uint32_t constant_id{0};
        /// @brief Byte offset into the data block.
        uint32_t offset{0};
        /// @brief Size in bytes.
        uint32_t size{0};
    };

    /**
     * @brief Everything a graphics pipeline is built from.
     *
     * The description states the state groups, the vertex input and the attachment
     * formats; what is not here is a viewport and a sample count, because one is
     * dynamic and the other is a device property.
     */
    struct GraphicsPipelineBuildDescription {
        /// @brief The state groups, vertex input and attachment formats.
        PipelineDescription pipeline;
        /// @brief The layout its bindings and constants belong to.
        PipelineLayoutHandle layout{};
        /// @brief The shaders it runs, one per stage.
        std::vector<GraphicsPipelineStage> stages;
        /// @brief Where each specialization constant sits in `specialization_data`.
        std::vector<SpecializationConstant> specialization_entries;
        /// @brief The values themselves, packed.
        std::vector<std::byte> specialization_data;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_PIPELINE_BUILDER_INCLUDED
