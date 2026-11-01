#include "Rhi/Submission/SubmissionHelper.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Submission/EpochTracker.h"
#include "Rhi/Texture/Texture.h"

#include <SDL3/SDL.h>
#include <stdexcept>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    /**
     * @brief Uploads on a backend that has a direct write path.
     *
     * The Vulkan helper exists because a device-local buffer cannot be written from the host:
     * every upload becomes a staging buffer, a copy command, a barrier and a batch to retire
     * them. The standard interface has `wgpuQueueWriteBuffer` and `wgpuQueueWriteTexture`,
     * which copy from host memory into the queue directly. There is no staging buffer to
     * create, no copy to record and nothing to retire.
     *
     * Two consequences worth stating rather than hiding:
     *
     * - **A batch is a bookkeeping state, not a device object.** `ExecuteSubmission` has
     *   nothing to submit because each write was already queued when it was enqueued. It
     *   still enforces the helper's state machine, so a caller that misuses the order is
     *   told here rather than discovering it later.
     * - **A texture clear needs a pass.** There is no queue-level clear for a texture, so it
     *   records a render pass whose load operation is Clear and submits it immediately.
     */
    struct SubmissionHelper::impl {
        WGPUDevice device{nullptr};
        WGPUQueue queue{nullptr};
        enum class State { Reset, Enqueued, Submitted } state{State::Reset};
    };

    SubmissionHelper::SubmissionHelper(
        const DeviceInterface &device_interface, const AllocatorState &allocator, EpochTracker &epoch_tracker
    ) :
        m_device_interface(device_interface), m_allocator(allocator), m_epoch_tracker(epoch_tracker),
        pimpl(std::make_unique<impl>()) {
        pimpl->device = ToWgpuDevice(device_interface.GetDevice());
        pimpl->queue = device_interface.GetQueueInfo().graphicsQueue;
    }

    SubmissionHelper::~SubmissionHelper() = default;

    void SubmissionHelper::EnqueueBufferSubmission(
        const DeviceBuffer &buffer, std::span<const std::byte> data, size_t buffer_offset
    ) {
        if (pimpl->state == impl::State::Submitted) {
            throw std::runtime_error(
                "SubmissionHelper: enqueueing while a deferred batch is pending; call "
                "OnBatchComplete first."
            );
        }
        // The write is queued here. There is nothing to copy into and nothing to free.
        wgpuQueueWriteBuffer(
            pimpl->queue, ToWgpuBuffer(buffer.GetBuffer()), buffer_offset, data.data(), data.size()
        );
        pimpl->state = impl::State::Enqueued;
    }

    void SubmissionHelper::EnqueueTextureBufferSubmission(
        const Texture &texture, std::span<const std::byte> data
    ) {
        if (pimpl->state == impl::State::Submitted) {
            throw std::runtime_error(
                "SubmissionHelper: enqueueing while a deferred batch is pending; call "
                "OnBatchComplete first."
            );
        }
        const Texture::TextureDesc &desc = texture.GetTextureDescription();

        WGPUTexelCopyBufferLayout layout{};
        // The seam's contract says the data covers the whole image, so the row stride is the
        // image's width times its texel size.
        layout.bytesPerRow = desc.width * GetPixelSize(desc.format);
        layout.rowsPerImage = desc.height;

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = ToWgpuTexture(texture.GetImage());
        destination.aspect = WGPUTextureAspect_All;

        const WGPUExtent3D extent{desc.width, desc.height, desc.depth};
        wgpuQueueWriteTexture(pimpl->queue, &destination, data.data(), data.size(), &layout, &extent);
        pimpl->state = impl::State::Enqueued;
    }

    void SubmissionHelper::EnqueueTextureClear(const Texture &texture, std::tuple<float, float, float, float> color) {
        if (pimpl->state == impl::State::Submitted) {
            throw std::runtime_error(
                "SubmissionHelper: enqueueing while a deferred batch is pending; call "
                "OnBatchComplete first."
            );
        }
        // No queue-level clear exists for a texture, so this records the smallest pass that
        // does it: one colour attachment, load operation Clear, store.
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(pimpl->device, nullptr);
        WGPURenderPassColorAttachment attachment{};
        attachment.view = ToWgpuTextureView(texture.GetImageView());
        attachment.loadOp = WGPULoadOp_Clear;
        attachment.storeOp = WGPUStoreOp_Store;
        attachment.clearValue = WGPUColor{
            std::get<0>(color), std::get<1>(color), std::get<2>(color), std::get<3>(color)
        };

        WGPURenderPassDescriptor pass_desc{};
        pass_desc.colorAttachmentCount = 1;
        pass_desc.colorAttachments = &attachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(pimpl->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        pimpl->state = impl::State::Enqueued;
    }

    void SubmissionHelper::EnqueueTextureClear(const Texture &texture, float depth) {
        if (pimpl->state == impl::State::Submitted) {
            throw std::runtime_error(
                "SubmissionHelper: enqueueing while a deferred batch is pending; call "
                "OnBatchComplete first."
            );
        }
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(pimpl->device, nullptr);
        WGPURenderPassDepthStencilAttachment attachment{};
        attachment.view = ToWgpuTextureView(texture.GetImageView());
        attachment.depthLoadOp = WGPULoadOp_Clear;
        attachment.depthStoreOp = WGPUStoreOp_Store;
        attachment.depthClearValue = depth;

        WGPURenderPassDescriptor pass_desc{};
        pass_desc.depthStencilAttachment = &attachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(pimpl->queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);
        pimpl->state = impl::State::Enqueued;
    }

    void SubmissionHelper::ExecuteSubmission(TimelineSignal) {
        if (pimpl->state == impl::State::Submitted) {
            throw std::runtime_error("SubmissionHelper: a deferred batch is already pending.");
        }
        // Every enqueue already reached the queue; there is nothing left to submit. The
        // signal has no meaning because there is no timeline semaphore here.
        pimpl->state = impl::State::Submitted;
    }

    void SubmissionHelper::ExecuteSubmissionImmediately() {
        if (pimpl->state == impl::State::Submitted) {
            throw std::runtime_error("SubmissionHelper: a deferred batch is already pending.");
        }
        pimpl->state = impl::State::Reset;
    }

    void SubmissionHelper::OnBatchComplete() {
        if (pimpl->state == impl::State::Enqueued) {
            throw std::runtime_error(
                "SubmissionHelper: OnBatchComplete while operations are enqueued but not "
                "submitted."
            );
        }
        pimpl->state = impl::State::Reset;
    }
} // namespace Engine::Rhi

