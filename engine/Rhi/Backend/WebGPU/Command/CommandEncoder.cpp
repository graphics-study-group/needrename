#include "Rhi/Command/CommandEncoder.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"
#include "Rhi/RhiTypes.h"

#include <SDL3/SDL.h>
#include <array>
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
         * @brief The host-visible buffer that carries every push-constant block.
         *
         * One buffer, one window per record, and a dynamic offset chosen when the bind group is
         * bound. Two facts make this safe rather than a guess:
         *
         * - a queue write is ordered against submissions on the same queue, so a write a later
         *   frame makes cannot be observed by an earlier frame's commands;
         * - every window is `k_constant_window` bytes and the cursor restarts at zero for each
         *   command encoder, so the whole frame fits in the buffer as long as it records at most
         *   `capacity / k_constant_window` of them.
         */
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

        struct ConstantRing {
            WGPUBuffer buffer{nullptr};
            WGPUBindGroupLayout layout{nullptr};
            WGPUBindGroup group{nullptr};
            size_t capacity{0};

            bool Ready() const noexcept {
                return buffer != nullptr && group != nullptr;
            }
        };

        ConstantRing &Ring() {
            static ConstantRing ring{};
            if (ring.Ready()) {
                return ring;
            }            WGPUDevice device = RegisteredWgpuDevice();
            if (device == nullptr) {
                return ring;
            }

            const WGPUBindGroupLayoutEntry entry = ConstantBlockLayoutEntry();
            WGPUBindGroupLayoutDescriptor layout_desc{};
            layout_desc.entryCount = 1;
            layout_desc.entries = &entry;
            layout_desc.label = WGPUStringView{"Constant Block Layout", 23};
            ring.layout = wgpuDeviceCreateBindGroupLayout(device, &layout_desc);

            ring.capacity = 1u << 20; // 1 MiB: 4096 records per frame.
            WGPUBufferDescriptor buffer_desc{};
            buffer_desc.size = ring.capacity;
            buffer_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            buffer_desc.mappedAtCreation = false;
            buffer_desc.label = WGPUStringView{"Constant Ring", 13};
            ring.buffer = wgpuDeviceCreateBuffer(device, &buffer_desc);

            WGPUBindGroupEntry group_entry{};
            group_entry.binding = 0;
            group_entry.buffer = ring.buffer;
            group_entry.offset = 0;
            group_entry.size = k_constant_window;

            WGPUBindGroupDescriptor group_desc{};
            group_desc.layout = ring.layout;
            group_desc.entryCount = 1;
            group_desc.entries = &group_entry;
            group_desc.label = WGPUStringView{"Constant Block Bind Group", 25};
            ring.group = wgpuDeviceCreateBindGroup(device, &group_desc);
            return ring;
        }

        /// @brief Publish one push-constant block and select its window for the current pass.
        void RecordConstantWindow(
            WGPURenderPassEncoder render_pass,
            WGPUComputePassEncoder compute_pass,
            size_t &cursor,
            std::span<const std::byte> data
        ) {
            if (render_pass == nullptr && compute_pass == nullptr) {
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
            ConstantRing &ring = Ring();
            if (!ring.Ready()) {
                Unsupported("PushConstants", "the constant ring could not be created on this device.");
            }
            if (cursor + k_constant_window > ring.capacity) {
                throw std::length_error(
                    std::format(
                        "WebGPU: the frame recorded more constant blocks than the {} byte ring holds.",
                        ring.capacity
                    )
                );
            }

            const size_t offset = cursor;
            cursor += k_constant_window;

            // The window is zero-filled before the block is copied into it: a shader reads its
            // whole declared struct, and a caller that pushes fewer bytes than the block is -
            // which every draw in this engine does, it pushes the model matrix and leaves the
            // two indices out - would otherwise read whatever the ring held there before.
            std::array<std::byte, k_constant_window> window{};
            std::copy(data.begin(), data.end(), window.begin());

            // Published before the submission that reads it: a queue write is ordered against
            // the submissions on the same queue, so the window is visible to the draws that
            // follow in this frame's command buffer.
            wgpuQueueWriteBuffer(
                RegisteredWgpuQueue(), ring.buffer, offset, window.data(), window.size()
            );

            const uint32_t dynamic_offset = static_cast<uint32_t>(offset);
            if (render_pass != nullptr) {
                wgpuRenderPassEncoderSetBindGroup(render_pass, k_constant_bind_group, ring.group, 1, &dynamic_offset);
            } else {
                wgpuComputePassEncoderSetBindGroup(
                    compute_pass, k_constant_bind_group, ring.group, 1, &dynamic_offset
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
        /// Whether the pass that is open has had a pipeline set in it. A draw before that is
        /// what the interface reports as "Render pipeline must be set".
        bool pass_pipeline_set{false};
        /// How far into the constant ring this command buffer has written. It restarts at zero
        /// for every encoder, and queue ordering makes the reuse across frames safe.
        size_t constant_cursor{0};
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
            // The INIT macro rather than `{}`: the structure's defaults are not all zero, and one
            // of them - `depthSlice` - means "layer zero of a 3D view" when it is, which is a
            // validation error against a colour attachment that is not 3D.
            WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
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

        WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
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
        pimpl->pass_pipeline_set = false;
        // The constant group is part of every pipeline layout that declares a constant range,
        // and a draw whose shader reads it must find it bound even before the first
        // `PushConstants` of the pass. Window zero is a valid window; a push replaces it.
        ConstantRing &ring = Ring();
        if (ring.Ready()) {
            const uint32_t offset = 0;
            wgpuRenderPassEncoderSetBindGroup(pimpl->render_pass, k_constant_bind_group, ring.group, 1, &offset);
        }
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
            ConstantRing &ring = Ring();
            if (ring.Ready()) {
                const uint32_t offset = 0;
                wgpuComputePassEncoderSetBindGroup(
                    pimpl->compute_pass, k_constant_bind_group, ring.group, 1, &offset
                );
            }
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
        if (!pipeline.IsValid()) {
        }
        pimpl->pass_pipeline_set = true;
        wgpuRenderPassEncoderSetPipeline(pimpl->render_pass, ToWgpuRenderPipeline(pipeline));
    }

    void CommandEncoder::PushConstants(std::span<const std::byte> data) {
        RecordConstantWindow(pimpl->render_pass, pimpl->compute_pass, pimpl->constant_cursor, data);
    }

    void CommandEncoder::SetKernelParams(std::span<const std::byte> data) {
        RecordConstantWindow(pimpl->render_pass, pimpl->compute_pass, pimpl->constant_cursor, data);
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
        if (!pimpl->pass_pipeline_set) {
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
        if (!pimpl->pass_pipeline_set) {
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
            wgpuCommandEncoderCopyTextureToBuffer(pimpl->encoder, &source, &destination, &copy_extent);
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
        wgpuCommandEncoderCopyTextureToBuffer(pimpl->encoder, &source, &destination, &copy_extent);

        for (uint32_t row = 0; row < extent.height; row++) {
            wgpuCommandEncoderCopyBufferToBuffer(
                pimpl->encoder,
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
        RetireStagingBuffer(pimpl->encoder, staging);
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