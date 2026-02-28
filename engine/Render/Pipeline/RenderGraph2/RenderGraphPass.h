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
    class RenderGraph2;
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
        std::function <void(vk::CommandBuffer, const RenderGraph2 &)> pass_function{};

        // Access registry
        std::unordered_map <RGTextureHandle, MemoryAccessTypeImageBits> image_access{};
        std::unordered_map <RGBufferHandle, MemoryAccessTypeBuffer> buffer_access{};

        // Attachments
        std::vector <RGAttachmentDesc2> color_attachments;
        RGAttachmentDesc2 depth_attachment;
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
         * Actual type and affinity of the pass are set to Graphics.
         */
        RenderGraphPassBuilder & SetRasterizerPassFunction (
            std::function <void(GraphicsCommandBuffer &, const RenderGraph2 &)> f
        ) noexcept;

        /**
         * @brief Set up a pass function for compute shader invocations.
         * 
         * Actual type of the pass is set to Compute.
         * Affinity is set to Graphics. See `SetAffinity()` for more
         * details.
         */
        RenderGraphPassBuilder & SetComputePassFunction (
            std::function <void(ComputeCommandBuffer &, const RenderGraph2 &)> f
        ) noexcept;

        /**
         * @brief Set affinity of the pass.
         * 
         * Affinity determines whether the workload will be redistributed to
         * be carried out asynchronously.
         * 
         * Typically you want all workloads to be distributed synchronously onto
         * the Graphics core if you don't know what you are doing, which is the
         * default behavior.
         * Asynchronous Compute, if implemented badly, could harm overall
         * performance instead.
         * 
         * @see Here are some references on Asynchronous Compute:
         * https://developer.nvidia.com/blog/advanced-api-performance-async-compute-and-overlap/
         * https://gpuopen.com/learn/concurrent-execution-asynchronous-queues/
         */
        RenderGraphPassBuilder & SetAffinity(
            RenderGraphPassAffinity affinity
        ) noexcept {
            pass.affinity = affinity;
            return *this;
        }

        /**
         * @brief Mark an image for access.
         */
        RenderGraphPassBuilder & UseImage (
            RGTextureHandle handle,
            MemoryAccessTypeImageBits access
        ) noexcept {
            pass.image_access[handle] = access;
            return *this;
        }

        /**
         * @brief Mark a buffer for access.
         * 
         * @note Currently buffer dependency will introduce a global barrier
         * instead of a per-buffer barrier. It seems that most graphics drivers
         * does not support buffer ganularity synchronization after all.
         */
        RenderGraphPassBuilder & UseBuffer (
            RGBufferHandle handle,
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
         * 
         * Implementationwise, it is marked by a special resource handle zero.
         */
        RenderGraphPassBuilder & SetGlobalAccess (
            MemoryAccessTypeBuffer access
        ) noexcept {
            pass.buffer_access[static_cast<RGBufferHandle>(0)] = access;
            return *this;
        }

        /**
         * @brief Append a new color attachment.
         * 
         * This attachment will be automatically marked for read and write.
         */
        RenderGraphPassBuilder & AppendColorAttachment (
            RGAttachmentDesc2 attachment
        ) noexcept {
            pass.color_attachments.push_back(attachment);
            pass.image_access[attachment.rt_handle] = MemoryAccessTypeImageBits::ColorAttachmentDefault;
            return *this;
        }

        /**
         * @brief Set depth stencil attachment.
         * 
         * This attachment will be automatically marked for read and write.
         */
        RenderGraphPassBuilder & SetDepthStencilAttachment (
            RGAttachmentDesc2 attachment
        ) noexcept {
            pass.depth_attachment = attachment;
            pass.image_access[attachment.rt_handle] = MemoryAccessTypeImageBits::DepthStencilAttachmentDefault;
            return *this;
        }

        /**
         * @brief Wrap the rasterizer pass function between render pass
         * beginning and ending commands.
         * 
         * You should only call this method after all attachments and the pass
         * function are specified.
         * 
         * Rendering area is determined by the size of the smallest color
         * attachment.
         */
        RenderGraphPassBuilder & WrapRenderPass() noexcept;

        [[nodiscard]]
        RenderGraphPass Get() noexcept {
            return pass;
        }

    };
}

#endif // PIPELINE_RENDERGRAPH2_RENDERGRAPHPASS
