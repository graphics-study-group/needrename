#ifndef PIPELINE_RENDERGRAPH2_RENDERGRAPHSTRUCT_INCLUDED
#define PIPELINE_RENDERGRAPH2_RENDERGRAPHSTRUCT_INCLUDED

#include <functional>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "Render/Memory/MemoryAccessTypes.h"
#include "Render/Pipeline/PipelineRuntimeInfo.h"
#include "Render/RenderSystem/ResizableRTTManager.h"

namespace Engine {
    class RenderGraph2;
    class RenderTargetTexture;

    using RenderTargetTextureVariant = std::variant<RenderTargetTexture *, RRTTHandle>;
    using OwnedRenderTargetTextureVariant = std::variant<std::unique_ptr<RenderTargetTexture>, RRTTHandle>;

    /**
     * @brief Contextual data used in a render pass
     */
    struct RenderGraph2Context {
        RenderSystem *render_system;
        RenderGraph2 *render_graph;
        uint32_t frame_in_flight;
    };

    /**
     * @brief Visitor for render target textures.
     */
    struct RenderTargetTextureVariantVisitor {
        RenderTargetTexture *operator()(RenderTargetTexture *p) {
            return p;
        }

        RenderTargetTexture *operator()(RRTTHandle h) {
            return &h.Resolve();
        }
    };

    /**
     * @brief Compiled render graph passes.
     *
     * Each pass corresponds to one command buffer and one submission. Multiple
     * render passes may be merged into one render graph pass after compilation.
     */
    struct RenderGraphCompiledPass {
        vk::PipelineStageFlags2 wait_stage{}, signal_stage{};

        struct Subpass {
            std::vector<std::pair<RGTextureHandle, vk::ImageMemoryBarrier2>> image_barriers{};
            std::vector<std::pair<RGBufferHandle, vk::BufferMemoryBarrier2>> buffer_barriers{};
            vk::MemoryBarrier2 global_memory_barrier{};

            PipelineRuntimeInfoPerRendering per_rendering_info{};

            std::function<void(vk::CommandBuffer, const RenderGraph2Context)> pass_work{};
        };
        std::vector<Subpass> subpasses{};
    };

    /**
     * @brief Extra info used for constructing a render graph.
     */
    struct RenderGraph2ExtraInfo {
        std::unordered_map<RGTextureHandle, MemoryAccessTypeImageBits> first_persistent_texture_access,
            last_persistent_texture_access;

        std::unordered_map<RGTextureHandle, OwnedRenderTargetTextureVariant> transient_texture_storage;

        std::unordered_map<RGTextureHandle, RenderTargetTextureVariant> texture_mapping;

        std::unordered_map<RGBufferHandle, const DeviceBuffer *> buffer_mapping;
    };
} // namespace Engine

#endif // PIPELINE_RENDERGRAPH2_RENDERGRAPHSTRUCT_INCLUDED
