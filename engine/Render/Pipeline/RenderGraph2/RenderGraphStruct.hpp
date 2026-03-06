#ifndef PIPELINE_RENDERGRAPH2_RENDERGRAPHSTRUCT
#define PIPELINE_RENDERGRAPH2_RENDERGRAPHSTRUCT

#include <vector>
#include <functional>
#include <vulkan/vulkan.hpp>

#include "Render/Memory/MemoryAccessTypes.h"
#include "Render/Pipeline/PipelineRuntimeInfo.h"

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
        vk::PipelineStageFlags2 wait_stage{}, signal_stage{};

        struct Subpass {
            std::vector <std::pair<RGTextureHandle, vk::ImageMemoryBarrier2>> image_barriers {};
            std::vector <std::pair<RGBufferHandle, vk::BufferMemoryBarrier2>> buffer_barriers {};
            vk::MemoryBarrier2 global_memory_barrier {};

            PipelineRuntimeInfoPerRendering per_rendering_info {};

            std::function <void(vk::CommandBuffer, const RenderGraph2 & rg)> pass_work {};
        };
        std::vector <Subpass> subpasses {};
    };

    /**
     * @brief Extra info used for constructing a render graph.
     */
    struct RenderGraph2ExtraInfo {
        std::unordered_map <
            RGTextureHandle, MemoryAccessTypeImageBits
        > first_persistent_texture_access, last_persistent_texture_access;

        std::unordered_map <
            RGTextureHandle, std::unique_ptr <RenderTargetTexture>
        > transient_texture_storage;

        std::unordered_map <
            RGTextureHandle, RenderTargetTexture *
        > texture_mapping;

        std::unordered_map <
            RGBufferHandle, const DeviceBuffer *
        > buffer_mapping;
    };
}

#endif // PIPELINE_RENDERGRAPH2_RENDERGRAPHSTRUCT
