#ifndef PIPELINE_RENDERGRAPH2_RENDERGRAPHSTRUCT
#define PIPELINE_RENDERGRAPH2_RENDERGRAPHSTRUCT

#include <vector>
#include <functional>
#include <vulkan/vulkan.hpp>

#include "Render/Memory/MemoryAccessTypes.h"

namespace Engine {
    class RenderGraph2;
    class RenderTargetTexture;

    /**
     * @brief Compiled render graph passes.
     * 
     * Each pass corresponds to one command buffer and one submission. Multiple
     * render passes may be merged into one render graph pass after compilation.
     */
    struct RenderGraphCompiledPass {
        vk::PipelineStageFlags2 wait_stage, signal_stage;

        std::vector <
            std::function <void(vk::CommandBuffer, const RenderGraph2 & rg)>
        > pass_works;
    };

    /**
     * @brief Extra info used for constructing a render graph.
     */
    struct RenderGraph2ExtraInfo {
        std::unordered_map <
            int32_t, MemoryAccessTypeImageBits
        > first_persistent_texture_access, last_persistent_texture_access;

        std::unordered_map <
            int32_t, std::unique_ptr <RenderTargetTexture>
        > transient_texture_storage;

        std::unordered_map <
            int32_t, RenderTargetTexture *
        > texture_mapping;

        std::unordered_map <
            int32_t, const DeviceBuffer *
        > buffer_mapping;
    };
}

#endif // PIPELINE_RENDERGRAPH2_RENDERGRAPHSTRUCT
