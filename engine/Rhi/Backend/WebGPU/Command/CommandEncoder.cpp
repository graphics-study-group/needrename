#include "Rhi/Command/CommandEncoder.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/RhiTypes.h"

#include <SDL3/SDL.h>
#include <stdexcept>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        /// @brief Report a call this backend cannot honour, and say what would.
        [[noreturn]] void Unsupported(const char *what, const char *why) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: %s is not implemented. %s", what, why);
            throw std::logic_error(std::string{"WebGPU: "} + what + " is not implemented. " + why);
        }
    } // namespace

    /**
     * @brief The seam's recorder on a backend with three pass encoders instead of one.
     *
     * The shape difference is the largest in the seam. Vulkan records into one command
     * buffer and a render pass is a lifetime inside it; the standard interface hands out
     * a `WGPURenderPassEncoder` or a `WGPUComputePassEncoder` that exists only while the
     * pass is open, and bind calls go to that object rather than to the command
     * encoder. The `impl` therefore holds whichever pass is current.
     *
     * Two calls have no counterpart at all, and they are the interesting ones:
     *
     * - `Barrier` does nothing. There is no explicit transition: the standard interface
     *   derives the work from how a resource is used in each pass, and a caller that
     *   says "this needs a transition" is describing the Vulkan model. Silently doing
     *   nothing is correct here and is not a stub - the same call on the same resource
     *   produces the same frame, which is what a portable barrier is for.
     *
     * - `PushConstants` and `SetKernelParams` throw. There is no push-constant block; a
     *   kernel parameter block is a uniform buffer bound at the slot the bake records in
     *   `.iface.json` under `constant_block` - `{set: 1, binding: 0, size: 12}` for
     *   `radix_scatter.comp`, so the slot is decided. What is missing is the ring that holds
     *   one window per dispatch; throwing until then is the answer that does not silently
     *   drop the values.
     */
    struct CommandEncoder::impl {
        WGPUCommandEncoder encoder{nullptr};
        /// Whichever pass is open, if any. At most one of these is non-null.
        WGPURenderPassEncoder render_pass{nullptr};
        WGPUComputePassEncoder compute_pass{nullptr};
        bool in_render_pass{false};
    };

    CommandEncoder::CommandEncoder(CommandBufferHandle handle) : pimpl(std::make_unique<impl>()) {
        pimpl->encoder = ToWgpuCommandEncoder(handle);
    }

    CommandEncoder::~CommandEncoder() {
        // A pass left open would be a caller bug, but leaking the encoder would be
        // worse: end it so the command buffer is still usable.
        if (pimpl && pimpl->render_pass != nullptr) {
            wgpuRenderPassEncoderEnd(pimpl->render_pass);
            wgpuRenderPassEncoderRelease(pimpl->render_pass);
        }
        if (pimpl && pimpl->compute_pass != nullptr) {
            wgpuComputePassEncoderEnd(pimpl->compute_pass);
            wgpuComputePassEncoderRelease(pimpl->compute_pass);
        }
    }

    CommandEncoder::CommandEncoder(CommandEncoder &&) noexcept = default;
    CommandEncoder &CommandEncoder::operator=(CommandEncoder &&) noexcept = default;

    void CommandEncoder::Barrier() {
        if (pimpl->in_render_pass) {
            throw std::logic_error(
                "Barrier() inside a render pass: a backend that has no transitions cannot honour it"
            );
        }
        // Nothing to record. See the class comment: the standard interface derives the
        // transition from use, so a ported barrier is a no-op that produces the same
        // frame rather than a stub that does nothing useful.
    }

    void CommandEncoder::Barrier(const ImageBarrier &) {
        Barrier();
    }

    void CommandEncoder::Barrier(std::span<const ImageBarrier>) {
        Barrier();
    }

    void CommandEncoder::BlitImage(const ImageBlitDescription &, bool) {
        Unsupported(
            "BlitImage",
            "A blit is a draw with a shader here; the seam's blit is a fixed-function copy."
        );
    }

    void CommandEncoder::BeginRenderPass(const RenderPassDescription &description) {
        if (pimpl->in_render_pass) {
            throw std::logic_error("BeginRenderPass called while a render pass is already open.");
        }
        std::vector<WGPURenderPassColorAttachment> color_attachments;
        color_attachments.reserve(description.color_attachments.size());
        for (const ColorAttachmentDescription &attachment : description.color_attachments) {
            WGPURenderPassColorAttachment color{};
            color.view = ToWgpuTextureView(attachment.view);
            color.loadOp = attachment.load == LoadOperation::Clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
            color.storeOp = attachment.store == StoreOperation::Store ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
            color.clearValue = WGPUColor{
                attachment.clear.r,
                attachment.clear.g,
                attachment.clear.b,
                attachment.clear.a
            };
            color_attachments.push_back(color);
        }

        WGPURenderPassDepthStencilAttachment depth{};
        const bool has_depth = description.depth_attachment.view.IsValid();
        if (has_depth) {
            depth.view = ToWgpuTextureView(description.depth_attachment.view);
            depth.depthLoadOp = description.depth_attachment.load == LoadOperation::Clear
                                    ? WGPULoadOp_Clear
                                    : WGPULoadOp_Load;
            depth.depthStoreOp = WGPUStoreOp_Store;
            depth.depthClearValue = description.depth_attachment.clear_depth;
        }

        WGPURenderPassDescriptor pass_desc{};
        pass_desc.colorAttachmentCount = color_attachments.size();
        pass_desc.colorAttachments = color_attachments.data();
        pass_desc.depthStencilAttachment = has_depth ? &depth : nullptr;

        pimpl->render_pass = wgpuCommandEncoderBeginRenderPass(pimpl->encoder, &pass_desc);
        pimpl->in_render_pass = true;
    }

    void CommandEncoder::EndRenderPass() {
        if (!pimpl->in_render_pass || pimpl->render_pass == nullptr) {
            throw std::logic_error("EndRenderPass called with no render pass open.");
        }
        wgpuRenderPassEncoderEnd(pimpl->render_pass);
        wgpuRenderPassEncoderRelease(pimpl->render_pass);
        pimpl->render_pass = nullptr;
        pimpl->in_render_pass = false;
    }

    void CommandEncoder::BindComputePipeline(PipelineHandle pipeline, PipelineLayoutHandle layout) {
        static_cast<void>(layout);
        if (pimpl->in_render_pass) {
            throw std::logic_error("BindComputePipeline inside a render pass.");
        }
        // The seam has no BeginComputePass: the first compute call opens one, because a
        // caller does not say when compute starts. EndComputePass closes it, and so does
        // the destructor.
        if (pimpl->compute_pass == nullptr) {
            pimpl->compute_pass = wgpuCommandEncoderBeginComputePass(pimpl->encoder, nullptr);
        }
        wgpuComputePassEncoderSetPipeline(pimpl->compute_pass, ToWgpuComputePipeline(pipeline));
    }

    void CommandEncoder::EndComputePass() {
        if (pimpl->compute_pass == nullptr) {
            return;
        }
        wgpuComputePassEncoderEnd(pimpl->compute_pass);
        wgpuComputePassEncoderRelease(pimpl->compute_pass);
        pimpl->compute_pass = nullptr;
    }

    void CommandEncoder::BindGraphicsPipeline(PipelineHandle pipeline, PipelineLayoutHandle layout) {
        static_cast<void>(layout);
        if (!pimpl->in_render_pass || pimpl->render_pass == nullptr) {
            throw std::logic_error("BindGraphicsPipeline outside a render pass.");
        }
        wgpuRenderPassEncoderSetPipeline(pimpl->render_pass, ToWgpuRenderPipeline(pipeline));
    }

    void CommandEncoder::PushConstants(std::span<const std::byte>) {
        Unsupported(
            "PushConstants",
            "There is no push-constant block here. A graphics parameter block becomes a "
            "uniform buffer at the slot the bake records, and the ring that holds those "
            "windows is not built yet."
        );
    }

    void CommandEncoder::SetKernelParams(std::span<const std::byte>) {
        Unsupported(
            "SetKernelParams",
            "Kernel parameters are a uniform buffer bound at the slot the bake records under "
            "constant_block; the ring that holds one window per dispatch is not built yet."
        );
    }

    void CommandEncoder::BindGraphicsDescriptorSet(
        uint32_t index, DescriptorSetHandle set, PipelineLayoutHandle, std::span<const uint32_t>
    ) {
        if (!pimpl->in_render_pass || pimpl->render_pass == nullptr) {
            throw std::logic_error("BindGraphicsDescriptorSet outside a render pass.");
        }
        wgpuRenderPassEncoderSetBindGroup(pimpl->render_pass, index, ToWgpuBindGroup(set), 0, nullptr);
    }

    void CommandEncoder::BindDescriptorSet(uint32_t set_index, DescriptorSetHandle set) {
        if (pimpl->in_render_pass && pimpl->render_pass != nullptr) {
            wgpuRenderPassEncoderSetBindGroup(pimpl->render_pass, set_index, ToWgpuBindGroup(set), 0, nullptr);
        } else if (pimpl->compute_pass != nullptr) {
            wgpuComputePassEncoderSetBindGroup(pimpl->compute_pass, set_index, ToWgpuBindGroup(set), 0, nullptr);
        } else {
            throw std::logic_error("BindDescriptorSet outside any pass.");
        }
    }

    void CommandEncoder::BindVertexBuffers(
        std::span<const BufferHandle> buffers, std::span<const uint64_t> offsets
    ) {
        if (!pimpl->in_render_pass || pimpl->render_pass == nullptr) {
            throw std::logic_error("BindVertexBuffers outside a render pass.");
        }
        for (size_t i = 0; i < buffers.size(); i++) {
            wgpuRenderPassEncoderSetVertexBuffer(
                pimpl->render_pass, static_cast<uint32_t>(i), ToWgpuBuffer(buffers[i]),
                i < offsets.size() ? offsets[i] : 0, WGPU_WHOLE_SIZE
            );
        }
    }

    void CommandEncoder::BindIndexBuffer(BufferHandle buffer, uint64_t offset, bool wide) {
        if (!pimpl->in_render_pass || pimpl->render_pass == nullptr) {
            throw std::logic_error("BindIndexBuffer outside a render pass.");
        }
        wgpuRenderPassEncoderSetIndexBuffer(
            pimpl->render_pass, ToWgpuBuffer(buffer), wide ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
            offset, WGPU_WHOLE_SIZE
        );
    }

    void CommandEncoder::SetViewport(float width, float height, const Rect2D &scissor) {
        if (!pimpl->in_render_pass || pimpl->render_pass == nullptr) {
            throw std::logic_error("SetViewport outside a render pass.");
        }
        wgpuRenderPassEncoderSetViewport(pimpl->render_pass, 0.0f, 0.0f, width, height, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(
            pimpl->render_pass, scissor.offset.x, scissor.offset.y, scissor.extent.width, scissor.extent.height
        );
    }

    void CommandEncoder::DrawIndexed(
        uint32_t index_count,
        uint32_t instance_count,
        uint32_t first_index,
        int32_t vertex_offset,
        uint32_t first_instance
    ) {
        if (!pimpl->in_render_pass || pimpl->render_pass == nullptr) {
            throw std::logic_error("DrawIndexed outside a render pass.");
        }
        wgpuRenderPassEncoderDrawIndexed(
            pimpl->render_pass, index_count, instance_count, first_index, vertex_offset, first_instance
        );
    }

    void CommandEncoder::Draw(
        uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance
    ) {
        if (!pimpl->in_render_pass || pimpl->render_pass == nullptr) {
            throw std::logic_error("Draw outside a render pass.");
        }
        wgpuRenderPassEncoderDraw(
            pimpl->render_pass, vertex_count, instance_count, first_vertex, first_instance
        );
    }

    void CommandEncoder::CopyBuffer(BufferHandle source, BufferHandle destination, uint64_t size) {
        wgpuCommandEncoderCopyBufferToBuffer(
            pimpl->encoder, ToWgpuBuffer(source), 0, ToWgpuBuffer(destination), 0, size
        );
    }

    void CommandEncoder::CopyImageToBuffer(
        ImageHandle image, BufferHandle buffer, const Extent3D &extent, ImageAspect aspect
    ) {
        static_cast<void>(aspect);
        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = ToWgpuBuffer(buffer);
        destination.layout.bytesPerRow = extent.width * 4;
        destination.layout.rowsPerImage = extent.height;

        WGPUTexelCopyTextureInfo source{};
        source.texture = ToWgpuTexture(image);
        source.aspect = WGPUTextureAspect_All;

        const WGPUExtent3D copy_extent{extent.width, extent.height, extent.depth};
        wgpuCommandEncoderCopyTextureToBuffer(pimpl->encoder, &source, &destination, &copy_extent);
    }

    void CommandEncoder::DispatchCompute(
        uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z
    ) {
        if (pimpl->compute_pass == nullptr) {
            throw std::logic_error(
                "DispatchCompute with no compute pipeline bound: the standard interface has no "
                "current pipeline outside a pass, so bind one first."
            );
        }
        wgpuComputePassEncoderDispatchWorkgroups(
            pimpl->compute_pass, group_count_x, group_count_y, group_count_z
        );
    }

    CommandBufferHandle CommandEncoder::GetHandle() const noexcept {
        return {};
    }
} // namespace Engine::Rhi






