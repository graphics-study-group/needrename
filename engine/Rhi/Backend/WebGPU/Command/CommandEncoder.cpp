#include "Rhi/Command/CommandEncoder.h"

#include "Rhi/Backend/WebGPU/Command/RecordingState.h"
#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"
#include "Rhi/RhiTypes.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        /// @brief Report a call this backend cannot honour, and say what would.
        [[noreturn]] void Unsupported(const char *what, const char *why) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: %s is not implemented. %s", what, why);
            throw std::logic_error(std::string{"WebGPU: "} + what + " is not implemented. " + why);
        }

        /**
         * @brief Staging buffers whose copies are still recorded but not yet executed.
         *
         * A texture-to-buffer copy whose rows are not already 256-byte aligned is staged through
         * a buffer of its own. That buffer belongs to the commands, not to the recording call, so
         * it is kept until the command buffer that recorded it is destroyed - which the frame
         * manager does after the fence that proves the commands have run.
         */
        struct RetiredStaging {
            WGPUCommandEncoder encoder{nullptr};
            WGPUBuffer buffer{nullptr};
        };

        std::vector<RetiredStaging> &RetiredStagingList() {
            static std::vector<RetiredStaging> list;
            return list;
        }

        /// @brief Keep `buffer` alive until `encoder`'s command buffer is gone.
        void RetireStagingBuffer(WGPUCommandEncoder encoder, WGPUBuffer buffer) {
            RetiredStagingList().push_back(RetiredStaging{encoder, buffer});
        }

        /**
         * @brief Record one constant block into the command buffer's staging.
         *
         * The block is not published here. Recording appends a zero-filled window to the
         * command buffer's staging and binds that window's device offset; the bytes reach
         * the device in one queue write immediately before the submission that reads them
         * (`DeviceContext::Submit`). See design D3.
         *
         * The window is zero-filled before the block is copied into it: a shader reads its
         * whole declared struct, and a caller that pushes fewer bytes than the block is -
         * which every draw in this engine does, it pushes the model matrix and leaves the
         * two indices out - would otherwise read whatever the ring held there before.
         */
        void RecordConstantWindow(RecordingState &state, std::span<const std::byte> data) {
            if (state.render_pass == nullptr && state.compute_pass == nullptr) {
                throw std::logic_error("A constant block can only be set inside a pass.");
            }
            if (data.size() > k_constant_window) {
                throw std::length_error(
                    std::format(
                        "WebGPU: a constant block of {} bytes exceeds the {} byte window this "
                        "backend reserves for one.",
                        data.size(),
                        k_constant_window
                    )
                );
            }
            const ConstantRingRef ring = GetConstantRing();
            if (!ring.Ready()) {
                Unsupported("PushConstants", "the constant ring could not be created on this device.");
            }
            if (state.constant_staging.size() + k_constant_window > ring.capacity) {
                throw std::length_error(
                    std::format(
                        "WebGPU: one command buffer recorded more constant blocks than the {} byte "
                        "constant ring holds.",
                        ring.capacity
                    )
                );
            }

            const size_t offset = state.constant_staging.size();
            state.constant_staging.resize(offset + k_constant_window, std::byte{0});
            std::copy(data.begin(), data.end(), state.constant_staging.begin() + static_cast<ptrdiff_t>(offset));

            const uint32_t dynamic_offset = static_cast<uint32_t>(offset);
            if (state.render_pass != nullptr) {
                wgpuRenderPassEncoderSetBindGroup(
                    state.render_pass, k_constant_bind_group, ring.group, 1, &dynamic_offset
                );
            } else {
                wgpuComputePassEncoderSetBindGroup(
                    state.compute_pass, k_constant_bind_group, ring.group, 1, &dynamic_offset
                );
            }
        }
    } // namespace

    /**
     * @brief The seam's recorder on a backend with three pass encoders instead of one.
     *
     * The shape difference is the largest in the seam. Vulkan records into one command
     * buffer and a render pass is a lifetime inside it; the standard interface hands out
     * a `WGPURenderPassEncoder` or a `WGPUComputePassEncoder` that exists only while the
     * pass is open, and bind calls go to that object rather than to the command
     * encoder. The recording therefore lives in a `RecordingState` that the command
     * buffer owns - not in this object - because the same recording is finished and
     * submitted by units that never see an encoder (see `RecordingState.h`).
     *
     * Two calls have no counterpart at all, and they are the interesting ones:
     *
     * - `Barrier` does nothing. There is no explicit transition: the standard interface
     *   derives the work from how a resource is used in each pass, and a caller that
     *   says "this needs a transition" is describing the Vulkan model. Silently doing
     *   nothing is correct here and is not a stub - the same call on the same resource
     *   produces the same frame, which is what a portable barrier is for.
     *
     * - Compute scopes are derived rather than declared. The first compute pipeline bind
     *   opens one, and consecutive compute commands record into it; a copy, a clear, a
     *   render scope or the end of the recording closes it. The seam has no
     *   `BeginComputePass` because a caller does not say when compute starts.
     */
    struct CommandEncoder::impl {
        /// @brief The recording this encoder records into; owned by its command buffer.
        RecordingState *state{nullptr};
        /// @brief The encoder handle, kept so the destructor can tell a live state from a freed one.
        WGPUCommandEncoder encoder{nullptr};
        /// @brief A state this encoder had to create itself, for a handle no command buffer owns.
        std::unique_ptr<RecordingState> owned_state{};
    };

    CommandEncoder::CommandEncoder(CommandBufferHandle handle) : pimpl(std::make_unique<impl>()) {
        pimpl->encoder = ToWgpuCommandEncoder(handle);
        pimpl->state = FindRecordingState(pimpl->encoder);
        if (pimpl->state == nullptr) {
            // A handle this backend did not hand out: record through a state of this
            // encoder's own rather than through a null pointer. Nothing finishes it, which is
            // the same as any other recording nobody finishes.
            pimpl->owned_state.reset(CreateRecordingState(pimpl->encoder));
            pimpl->state = pimpl->owned_state.get();
        }
    }

    CommandEncoder::~CommandEncoder() {
        if (!pimpl) {
            return;
        }
        if (pimpl->owned_state != nullptr) {
            CloseOpenPass(*pimpl->owned_state);
            pimpl->owned_state.reset();
            return;
        }
        // A pass left open would be a caller bug, but leaking the encoder would be
        // worse: end it so the command buffer is still usable.
        //
        // The state belongs to the command buffer, and the two objects are held by different
        // owners with no ordering between them - the frame manager destroys its encoder array and
        // its command-buffer array independently. So the registry, not the cached pointer, decides
        // whether there is still a state to close: a command buffer that is already gone took its
        // recording state with it, and touching the old address would be a use-after-free.
        if (pimpl->encoder != nullptr) {
            RecordingState *live = FindRecordingState(pimpl->encoder);
            if (live != nullptr && live == pimpl->state) {
                CloseOpenPass(*live);
            }
        }
    }

    CommandEncoder::CommandEncoder(CommandEncoder &&) noexcept = default;
    CommandEncoder &CommandEncoder::operator=(CommandEncoder &&) noexcept = default;

    void *CommandEncoder::GetNativeRenderPass() const noexcept {
        // The pass exists only between BeginRenderPass and EndRenderPass, which is exactly when
        // a library that records into it itself needs to see it.
        if (pimpl == nullptr || pimpl->state == nullptr || !pimpl->state->in_render_pass) {
            return nullptr;
        }
        return pimpl->state->render_pass;
    }

    void CommandEncoder::Barrier() {
        if (pimpl->state->in_render_pass) {
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
        Unsupported("BlitImage", "A blit is a draw with a shader here; the seam's blit is a fixed-function copy.");
    }

    void CommandEncoder::BeginRenderPass(const RenderPassDescription &description) {
        if (pimpl->state->in_render_pass) {
            throw std::logic_error("BeginRenderPass called while a render pass is already open.");
        }
        // A compute scope cannot span a render scope: the two are different pass objects and
        // the standard interface orders them only as separate commands.
        CloseComputePass(*pimpl->state);

        std::vector<WGPURenderPassColorAttachment> color_attachments;
        color_attachments.reserve(description.color_attachments.size());
        for (const ColorAttachmentDescription &attachment : description.color_attachments) {
            // The INIT macro rather than `{}`: the structure's defaults are not all zero, and one
            // of them - `depthSlice` - means "layer zero of a 3D view" when it is, which is a
            // validation error against a colour attachment that is not 3D.
            WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            color.view = ToWgpuTextureView(attachment.view);
            color.loadOp = attachment.load == LoadOperation::Clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
            color.storeOp = attachment.store == StoreOperation::Store ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
            color.clearValue =
                WGPUColor{attachment.clear.r, attachment.clear.g, attachment.clear.b, attachment.clear.a};
            color_attachments.push_back(color);
        }

        WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        const bool has_depth = description.depth_attachment.view.IsValid();
        if (has_depth) {
            depth.view = ToWgpuTextureView(description.depth_attachment.view);
            depth.depthLoadOp =
                description.depth_attachment.load == LoadOperation::Clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
            depth.depthStoreOp = WGPUStoreOp_Store;
            depth.depthClearValue = description.depth_attachment.clear_depth;
        }

        WGPURenderPassDescriptor pass_desc{};
        pass_desc.colorAttachmentCount = color_attachments.size();
        pass_desc.colorAttachments = color_attachments.data();
        pass_desc.depthStencilAttachment = has_depth ? &depth : nullptr;
        pimpl->state->render_pass = wgpuCommandEncoderBeginRenderPass(pimpl->state->encoder, &pass_desc);
        pimpl->state->in_render_pass = true;
        pimpl->state->pass_pipeline_set = false;
        // The constant group is part of every pipeline layout that declares a constant range,
        // and a draw whose shader reads it must find it bound even before the first
        // `PushConstants` of the pass. Window zero is a valid window; a push replaces it.
        const ConstantRingRef ring = GetConstantRing();
        if (ring.Ready()) {
            const uint32_t offset = 0;
            wgpuRenderPassEncoderSetBindGroup(pimpl->state->render_pass, k_constant_bind_group, ring.group, 1, &offset);
        }
    }

    void CommandEncoder::EndRenderPass() {
        if (!pimpl->state->in_render_pass || pimpl->state->render_pass == nullptr) {
            throw std::logic_error("EndRenderPass called with no render pass open.");
        }
        CloseRenderPass(*pimpl->state);
    }

    void CommandEncoder::BindComputePipeline(PipelineHandle pipeline, PipelineLayoutHandle layout) {
        static_cast<void>(layout);
        if (pimpl->state->in_render_pass) {
            throw std::logic_error("BindComputePipeline inside a render pass.");
        }
        // The seam has no BeginComputePass: the first compute command opens one, and it stays
        // open for the compute commands that follow. Everything that cannot be recorded inside
        // a compute scope closes it first; `CommandBuffer::End` closes whatever is left.
        if (pimpl->state->compute_pass == nullptr) {
            pimpl->state->compute_pass = wgpuCommandEncoderBeginComputePass(pimpl->state->encoder, nullptr);
            CountComputePassOpened();
            const ConstantRingRef ring = GetConstantRing();
            if (ring.Ready()) {
                const uint32_t offset = 0;
                wgpuComputePassEncoderSetBindGroup(
                    pimpl->state->compute_pass, k_constant_bind_group, ring.group, 1, &offset
                );
            }
        }
        wgpuComputePassEncoderSetPipeline(pimpl->state->compute_pass, ToWgpuComputePipeline(pipeline));
    }

    void CommandEncoder::EndComputePass() {
        CloseComputePass(*pimpl->state);
    }

    void CommandEncoder::BindGraphicsPipeline(PipelineHandle pipeline, PipelineLayoutHandle layout) {
        static_cast<void>(layout);
        if (!pimpl->state->in_render_pass || pimpl->state->render_pass == nullptr) {
            throw std::logic_error("BindGraphicsPipeline outside a render pass.");
        }
        pimpl->state->pass_pipeline_set = true;
        wgpuRenderPassEncoderSetPipeline(pimpl->state->render_pass, ToWgpuRenderPipeline(pipeline));
    }

    void CommandEncoder::PushConstants(std::span<const std::byte> data) {
        RecordConstantWindow(*pimpl->state, data);
    }

    void CommandEncoder::SetKernelParams(std::span<const std::byte> data) {
        RecordConstantWindow(*pimpl->state, data);
    }

    void CommandEncoder::BindGraphicsDescriptorSet(
        uint32_t index, DescriptorSetHandle set, PipelineLayoutHandle, std::span<const uint32_t>
    ) {
        if (!pimpl->state->in_render_pass || pimpl->state->render_pass == nullptr) {
            throw std::logic_error("BindGraphicsDescriptorSet outside a render pass.");
        }
        wgpuRenderPassEncoderSetBindGroup(pimpl->state->render_pass, index, ToWgpuBindGroup(set), 0, nullptr);
    }

    void CommandEncoder::BindDescriptorSet(uint32_t set_index, DescriptorSetHandle set) {
        if (pimpl->state->in_render_pass && pimpl->state->render_pass != nullptr) {
            wgpuRenderPassEncoderSetBindGroup(pimpl->state->render_pass, set_index, ToWgpuBindGroup(set), 0, nullptr);
        } else if (pimpl->state->compute_pass != nullptr) {
            wgpuComputePassEncoderSetBindGroup(pimpl->state->compute_pass, set_index, ToWgpuBindGroup(set), 0, nullptr);
        } else {
            throw std::logic_error("BindDescriptorSet outside any pass.");
        }
    }

    void CommandEncoder::BindVertexBuffers(std::span<const BufferHandle> buffers, std::span<const uint64_t> offsets) {
        if (!pimpl->state->in_render_pass || pimpl->state->render_pass == nullptr) {
            throw std::logic_error("BindVertexBuffers outside a render pass.");
        }
        for (size_t i = 0; i < buffers.size(); i++) {
            wgpuRenderPassEncoderSetVertexBuffer(
                pimpl->state->render_pass,
                static_cast<uint32_t>(i),
                ToWgpuBuffer(buffers[i]),
                i < offsets.size() ? offsets[i] : 0,
                WGPU_WHOLE_SIZE
            );
        }
    }

    void CommandEncoder::BindIndexBuffer(BufferHandle buffer, uint64_t offset, bool wide) {
        if (!pimpl->state->in_render_pass || pimpl->state->render_pass == nullptr) {
            throw std::logic_error("BindIndexBuffer outside a render pass.");
        }
        wgpuRenderPassEncoderSetIndexBuffer(
            pimpl->state->render_pass,
            ToWgpuBuffer(buffer),
            wide ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
            offset,
            WGPU_WHOLE_SIZE
        );
    }

    void CommandEncoder::SetViewport(float width, float height, const Rect2D &scissor) {
        if (!pimpl->state->in_render_pass || pimpl->state->render_pass == nullptr) {
            throw std::logic_error("SetViewport outside a render pass.");
        }
        wgpuRenderPassEncoderSetViewport(pimpl->state->render_pass, 0.0f, 0.0f, width, height, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(
            pimpl->state->render_pass, scissor.offset.x, scissor.offset.y, scissor.extent.width, scissor.extent.height
        );
    }

    void CommandEncoder::DrawIndexed(
        uint32_t index_count,
        uint32_t instance_count,
        uint32_t first_index,
        int32_t vertex_offset,
        uint32_t first_instance
    ) {
        if (!pimpl->state->in_render_pass || pimpl->state->render_pass == nullptr) {
            throw std::logic_error("DrawIndexed outside a render pass.");
        }
        wgpuRenderPassEncoderDrawIndexed(
            pimpl->state->render_pass, index_count, instance_count, first_index, vertex_offset, first_instance
        );
    }

    void CommandEncoder::Draw(
        uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance
    ) {
        if (!pimpl->state->in_render_pass || pimpl->state->render_pass == nullptr) {
            throw std::logic_error("Draw outside a render pass.");
        }
        wgpuRenderPassEncoderDraw(
            pimpl->state->render_pass, vertex_count, instance_count, first_vertex, first_instance
        );
    }

    void CommandEncoder::CopyBuffer(BufferHandle source, BufferHandle destination, uint64_t size) {
        // A copy is not a compute command, so the scope the compute commands recorded into ends
        // before it: the standard interface has no ordering between a pass and a copy that
        // overlaps a pass, and the copy has to observe the writes that precede it.
        CloseComputePass(*pimpl->state);
        wgpuCommandEncoderCopyBufferToBuffer(
            pimpl->state->encoder, ToWgpuBuffer(source), 0, ToWgpuBuffer(destination), 0, size
        );
    }

    void CommandEncoder::FillBuffer(BufferHandle destination, uint64_t offset, uint64_t size, uint32_t value) {
        if (pimpl->state->in_render_pass) {
            throw std::logic_error("FillBuffer inside a render pass: end the pass first.");
        }
        if (size == 0u) {
            return;
        }
        if (value != 0u) {
            // The standard interface only has a clear, which writes zero. Refusing a
            // non-zero pattern is the answer that does not silently clear instead.
            throw std::invalid_argument("WebGPU: FillBuffer can only write zero on this backend.");
        }
        // The clear runs outside any pass, so the compute scope recorded so far ends here and
        // the later compute work opens a new one.
        CloseComputePass(*pimpl->state);
        // There is no barrier to record: a clear is ordered against the passes around it by
        // the implementation, which is the same reason `Barrier` is a no-op here.
        wgpuCommandEncoderClearBuffer(pimpl->state->encoder, ToWgpuBuffer(destination), offset, size);
    }

    void CommandEncoder::CopyImageToBuffer(
        ImageHandle image, BufferHandle buffer, const Extent3D &extent, ImageAspect aspect
    ) {
        static_cast<void>(aspect);
        // A texture-to-buffer copy is recorded on the command encoder, so an open compute scope
        // ends before it.
        CloseComputePass(*pimpl->state);
        // The interface requires every copied row to start at a multiple of 256 bytes, while
        // the seam's readback size is the image's tight size. Widening the pitch would write past
        // the destination, so the copy is staged: one texture-to-buffer copy with padded rows,
        // then one buffer-to-buffer copy per row that removes the padding again.
        constexpr uint32_t k_row_alignment = 256;
        const uint32_t tight_row = extent.width * 4;
        const uint32_t padded_row = (tight_row + k_row_alignment - 1) / k_row_alignment * k_row_alignment;

        WGPUTexelCopyTextureInfo source{};
        source.texture = ToWgpuTexture(image);
        source.aspect = WGPUTextureAspect_All;

        const WGPUBuffer destination_buffer = ToWgpuBuffer(buffer);
        if (source.texture == nullptr || destination_buffer == nullptr) {
            // Both are handles the backend has to know: a copy of an image or into a buffer it
            // cannot resolve is dropped with a line saying which, rather than handed to the
            // implementation as a null texture.
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "WebGPU: a read-back copy names %s%s%s; it is dropped.",
                source.texture == nullptr ? "an unknown image" : "an image",
                source.texture == nullptr && destination_buffer == nullptr ? " and " : "",
                destination_buffer == nullptr ? "an unknown buffer" : "a buffer"
            );
            return;
        }

        const WGPUExtent3D copy_extent{extent.width, extent.height, extent.depth};

        if (padded_row == tight_row) {
            WGPUTexelCopyBufferInfo destination{};
            destination.buffer = destination_buffer;
            destination.layout.bytesPerRow = tight_row;
            destination.layout.rowsPerImage = extent.height;
            wgpuCommandEncoderCopyTextureToBuffer(pimpl->state->encoder, &source, &destination, &copy_extent);
            return;
        }

        WGPUBufferDescriptor staging_desc{};
        staging_desc.size = static_cast<uint64_t>(padded_row) * extent.height * extent.depth;
        staging_desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        staging_desc.label = WGPUStringView{"Readback staging", 16};
        WGPUBuffer staging = wgpuDeviceCreateBuffer(RegisteredWgpuDevice(), &staging_desc);

        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = staging;
        destination.layout.bytesPerRow = padded_row;
        destination.layout.rowsPerImage = extent.height;
        wgpuCommandEncoderCopyTextureToBuffer(pimpl->state->encoder, &source, &destination, &copy_extent);

        for (uint32_t row = 0; row < extent.height; row++) {
            wgpuCommandEncoderCopyBufferToBuffer(
                pimpl->state->encoder,
                staging,
                static_cast<uint64_t>(row) * padded_row,
                destination_buffer,
                static_cast<uint64_t>(row) * tight_row,
                tight_row
            );
        }
        // The staging buffer is referenced by the commands recorded into this encoder, which run
        // when the command buffer is submitted - later than this call. Releasing it here would be
        // a use-after-free at submit, which is what a read-back that took this path crashed on.
        // It is retired instead, and released when the command buffer that used it goes away.
        RetireStagingBuffer(pimpl->state->encoder, staging);
    }

    void CommandEncoder::DispatchCompute(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
        if (pimpl->state->compute_pass == nullptr) {
            throw std::logic_error(
                "DispatchCompute with no compute pipeline bound: the standard interface has no "
                "current pipeline outside a pass, so bind one first."
            );
        }
        wgpuComputePassEncoderDispatchWorkgroups(
            pimpl->state->compute_pass, group_count_x, group_count_y, group_count_z
        );
        CountDispatch();
    }

    CommandBufferHandle CommandEncoder::GetHandle() const noexcept {
        return {};
    }
} // namespace Engine::Rhi

namespace Engine::Rhi {
    /// @brief Release the staging buffers retired against a command buffer.
    void ReleaseRetiredStagingBuffers(WGPUCommandEncoder encoder) {
        std::vector<RetiredStaging> &list = RetiredStagingList();
        for (auto entry = list.begin(); entry != list.end();) {
            if (entry->encoder == encoder) {
                wgpuBufferRelease(entry->buffer);
                entry = list.erase(entry);
            } else {
                ++entry;
            }
        }
    }
} // namespace Engine::Rhi
