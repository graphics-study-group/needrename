#ifndef PIPELINE_RENDERGRAPH2_RENDERGRAPHPASS
#define PIPELINE_RENDERGRAPH2_RENDERGRAPHPASS

#include <unordered_map>
#include <functional>

#include "Render/Pipeline/RenderGraph/RGAttachmentDesc.h"
#include "Render/Memory/MemoryAccessTypes.h"

namespace vk {
    class CommandBuffer;
}

namespace Engine {
    class RenderGraph;
    class GraphicsCommandBuffer;
    class ComputeCommandBuffer;

    enum class RenderGraphPassAffinity {
        None,
        Graphics,
        Compute,
        Transfer
    };

    struct RenderGraphPass {
        // Metadata
        std::string name{};
        /// If a render graph has side effects, it will never be culled from
        /// the final render graph. Its dependencies will always be respected.
        bool has_side_effects{true};
        RenderGraphPassAffinity affinity{}, actual_type{};

        // Actual pass function
        std::function <void(vk::CommandBuffer, const RenderGraph &)> pass_function{};

        // Access registry
        std::unordered_map <int32_t, MemoryAccessTypeImageBits> image_access{};
        std::unordered_map <int32_t, MemoryAccessTypeBuffer> buffer_access{};
        MemoryAccessTypeBuffer global_access{MemoryAccessTypeBufferBits::None};

        // Attachments
        std::vector <RGAttachmentDesc> color_attachments;
        RGAttachmentDesc depth_attachment;
    };

    class RenderGraphPassBuilder {
        RenderSystem & system;
        RenderGraphPass pass {};

    public:
        RenderGraphPassBuilder(RenderSystem & system) : system(system) {};

        RenderGraphPassBuilder & SetName(std::string name) noexcept {
            pass.name = name;
            return *this;
        }

        /**
         * @brief Set up a pass function for rasterizer.
         * 
         * Actual type and affinity of the pass is set to Graphics.
         */
        RenderGraphPassBuilder & SetRasterizerPassFunction (
            std::function <void(GraphicsCommandBuffer &, const RenderGraph &)> f
        ) noexcept;

        /**
         * @brief Set up a pass function for compute shader invocations.
         * 
         * Actual type and affinity of the pass is set to Compute.
         */
        RenderGraphPassBuilder & SetComputePassFunction (
            std::function <void(ComputeCommandBuffer &, const RenderGraph &)> f
        ) noexcept;

        RenderGraphPassBuilder & SetAffinity(
            RenderGraphPassAffinity affinity
        ) noexcept {
            pass.affinity = affinity;
            return *this;
        }

        RenderGraphPassBuilder & UseImage (
            int32_t handle,
            MemoryAccessTypeImageBits access
        ) noexcept {
            pass.image_access[handle] = access;
            return *this;
        }

        RenderGraphPassBuilder & UseBuffer (
            int32_t handle,
            MemoryAccessTypeBuffer access
        ) noexcept {
            pass.buffer_access[handle] = access;
            return *this;
        }

        /**
         * @brief Set up a global access flag.
         * 
         * Used to synchronize global scope accesses. This is typically carried
         * out by a semaphore or a memory barrier.
         */
        RenderGraphPassBuilder & SetGlobalAccess (
            MemoryAccessTypeBuffer access
        ) noexcept {
            pass.global_access = access;
            return *this;
        }

        RenderGraphPassBuilder & AppendColorAttachment (
            RGAttachmentDesc attachment
        ) noexcept {
            pass.color_attachments.push_back(attachment);
            pass.image_access[attachment.rt_handle] = MemoryAccessTypeImageBits::ColorAttachmentDefault;
            return *this;
        }

        RenderGraphPassBuilder & SetDepthStencilAttachment (
            RGAttachmentDesc attachment
        ) noexcept {
            pass.depth_attachment = attachment;
            pass.image_access[attachment.rt_handle] = MemoryAccessTypeImageBits::DepthStencilAttachmentDefault;
            return *this;
        }

        RenderGraphPass Get() noexcept {
            return pass;
        }

    };
}

#endif // PIPELINE_RENDERGRAPH2_RENDERGRAPHPASS
