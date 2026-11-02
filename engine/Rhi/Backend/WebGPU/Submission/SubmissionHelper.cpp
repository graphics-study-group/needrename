#include "Rhi/Submission/SubmissionHelper.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Submission/EpochTracker.h"
#include "Rhi/Texture/Texture.h"
#include "Rhi/Texture/TextureSubresourceView.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
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
        const size_t texel_size = GetPixelSize(desc.format);
        const uint32_t tight_row = static_cast<uint32_t>(desc.width * texel_size);
        // A queue write's rows have to start every 256 bytes. The seam's data is tight, so it is
        // copied row by row into a padded host buffer first - the alternative is a write the
        // validation refuses.
        const uint32_t padded_row = (tight_row + 255u) / 256u * 256u;
        // The data covers the whole image, faces included: a cube map or an array texture is one
        // write of `depth * layers` slices, and writing only the first would leave the rest
        // undefined - which samples as black rather than as an error.
        const uint32_t layers = desc.array_layers == 0 ? 1u : desc.array_layers;
        const uint32_t slices = (desc.depth == 0 ? 1u : desc.depth) * layers;

        const size_t source_tight_row = tight_row;
        const size_t source_layer_bytes = source_tight_row * desc.height;
        std::vector<std::byte> padded(
            static_cast<size_t>(padded_row) * desc.height * slices, std::byte{0}
        );
        for (uint32_t slice = 0; slice < slices; slice++) {
            for (uint32_t row = 0; row < desc.height; row++) {
                const size_t source_offset = static_cast<size_t>(slice) * source_layer_bytes
                                             + static_cast<size_t>(row) * source_tight_row;
                std::memcpy(
                    padded.data() + static_cast<size_t>(slice) * padded_row * desc.height
                        + static_cast<size_t>(row) * padded_row,
                    data.data() + source_offset,
                    std::min<size_t>(source_tight_row, data.size() - std::min(source_offset, data.size()))
                );
            }
        }

        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = padded_row;
        layout.rowsPerImage = desc.height;

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = ToWgpuTexture(texture.GetImage());
        destination.aspect = WGPUTextureAspect_All;

        const WGPUExtent3D extent{desc.width, desc.height, slices};
        wgpuQueueWriteTexture(pimpl->queue, &destination, padded.data(), padded.size(), &layout, &extent);
        pimpl->state = impl::State::Enqueued;
    }

    namespace {
        /// @brief Fill a host buffer with one texel value, laid out row by row for a write.
        std::vector<std::byte> BuildClearData(
            const Texture::TextureDesc &desc, uint32_t layer_count, const std::byte *texel, size_t texel_size
        ) {
            // A queue write's rows have to start every 256 bytes, so the host copy is padded and
            // the padding is written too. The padding is harmless: it lands outside the rows the
            // write covers when the layer is the last one.
            const uint32_t tight_row = desc.width * static_cast<uint32_t>(texel_size);
            const uint32_t padded_row = (tight_row + 255u) / 256u * 256u;
            const size_t layer_bytes = static_cast<size_t>(padded_row) * desc.height;
            std::vector<std::byte> data(layer_bytes * layer_count, std::byte{0});

            for (uint32_t layer = 0; layer < layer_count; layer++) {
                for (uint32_t row = 0; row < desc.height; row++) {
                    std::byte *destination =
                        data.data() + static_cast<size_t>(layer) * layer_bytes + static_cast<size_t>(row) * padded_row;
                    for (uint32_t column = 0; column < desc.width; column++) {
                        std::copy(texel, texel + texel_size, destination + static_cast<size_t>(column) * texel_size);
                    }
                }
            }
            return data;
        }

        void WriteTextureRows(
            WGPUQueue queue, const Texture &texture, const std::vector<std::byte> &data, uint32_t layer_count,
            uint32_t bytes_per_row
        ) {
            const Texture::TextureDesc &desc = texture.GetTextureDescription();
            WGPUTexelCopyTextureInfo destination{};
            destination.texture = ToWgpuTexture(texture.GetImage());
            destination.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyBufferLayout layout{};
            layout.bytesPerRow = bytes_per_row;
            layout.rowsPerImage = desc.height;

            WGPUExtent3D extent{desc.width, desc.height, desc.depth == 0 ? 1u : desc.depth};
            wgpuQueueWriteTexture(
                queue, &destination, data.data(), data.size() * layer_count, &layout, &extent
            );
            static_cast<void>(layer_count);
        }
    } // namespace

    void SubmissionHelper::EnqueueTextureClear(const Texture &texture, std::tuple<float, float, float, float> color) {
        if (pimpl->state == impl::State::Submitted) {
            throw std::runtime_error(
                "SubmissionHelper: enqueueing while a deferred batch is pending; call "
                "OnBatchComplete first."
            );
        }
        // A clear is a write, not a render pass: the interface has no way to clear a texture and
        // a pass would require the texture to be a render target, which an imported image or a
        // material placeholder is not. The value is written from the host instead.
        const Texture::TextureDesc &desc = texture.GetTextureDescription();
        const uint32_t layer_count = desc.array_layers == 0 ? 1 : desc.array_layers;

        std::array<std::byte, 4> texel{};
        texel[0] = static_cast<std::byte>(std::clamp(std::get<0>(color), 0.0f, 1.0f) * 255.0f + 0.5f);
        texel[1] = static_cast<std::byte>(std::clamp(std::get<1>(color), 0.0f, 1.0f) * 255.0f + 0.5f);
        texel[2] = static_cast<std::byte>(std::clamp(std::get<2>(color), 0.0f, 1.0f) * 255.0f + 0.5f);
        texel[3] = static_cast<std::byte>(std::clamp(std::get<3>(color), 0.0f, 1.0f) * 255.0f + 0.5f);

        const std::vector<std::byte> data = BuildClearData(desc, 1u, texel.data(), texel.size());
        const uint32_t padded_row = (desc.width * 4u + 255u) / 256u * 256u;
        WriteTextureRows(pimpl->queue, texture, data, 1u, padded_row);
        pimpl->state = impl::State::Enqueued;
    }

    void SubmissionHelper::EnqueueTextureClear(const Texture &texture, float depth) {
        if (pimpl->state == impl::State::Submitted) {
            throw std::runtime_error(
                "SubmissionHelper: enqueueing while a deferred batch is pending; call "
                "OnBatchComplete first."
            );
        }
        const Texture::TextureDesc &desc = texture.GetTextureDescription();
        const uint32_t layer_count = desc.array_layers == 0 ? 1 : desc.array_layers;

        static_cast<void>(layer_count);
        // A depth texture cannot be written from the host - the interface forbids
        // `wgpuQueueWriteTexture` into a depth format - so this one stays a render pass. It is
        // always legal for the textures the engine clears this way: a depth map exists to be
        // attached.
        static_cast<void>(desc);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(pimpl->device, nullptr);
        for (uint32_t layer = 0; layer < layer_count; layer++) {
            WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            attachment.view = ToWgpuTextureView(
                texture.GetImageView(TextureSubresourceRange{layer, 1, 0, desc.mipmap_levels})
            );
            attachment.depthLoadOp = WGPULoadOp_Clear;
            attachment.depthStoreOp = WGPUStoreOp_Store;
            attachment.depthClearValue = depth;

            WGPURenderPassDescriptor pass_desc{};
            pass_desc.depthStencilAttachment = &attachment;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }

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

