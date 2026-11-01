#include "Rhi/Command/CommandBuffer.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    /**
     * @brief One recording, and the buffer it becomes.
     *
     * The standard interface splits what Vulkan keeps together: a
     * `WGPUCommandEncoder` records, and `wgpuCommandEncoderFinish` turns it into a
     * `WGPUCommandBuffer` that can be submitted. `End` performs that turn and keeps the
     * result; `Reset` throws the finished buffer away and starts a new encoder.
     *
     * A command buffer here is therefore finished exactly once. `CommandEncoder` holds
     * the recording handle, so anything recorded after `End` targets an encoder whose
     * work has already been handed over - which makes `End` a point of no return rather
     * than a bookmark.
     */
    struct CommandBuffer::impl {
        WGPUDevice device{nullptr};
        WGPUCommandEncoder encoder{nullptr};
        /// Set by End; the submit path reads this rather than the encoder.
        WGPUCommandBuffer finished{nullptr};
        std::string name{};
    };

    CommandBuffer::CommandBuffer(std::unique_ptr<impl> p) noexcept : pimpl(std::move(p)) {
    }

    std::vector<CommandBuffer> CommandBuffer::Create(
        const DeviceContext &device_context, CommandBufferPool pool, uint32_t count
    ) {
        // There is no pool to allocate from: an encoder is created directly, and the
        // `pool` argument only says how long the caller intends to keep it.
        static_cast<void>(pool);
        const WGPUDevice device = ToWgpuDevice(device_context.GetDevice());

        std::vector<CommandBuffer> buffers;
        buffers.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            auto state = std::make_unique<impl>();
            state->device = device;
            state->encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
            buffers.push_back(CommandBuffer(std::move(state)));
        }
        return buffers;
    }

    CommandBuffer::~CommandBuffer() {
        if (!pimpl) {
            return;
        }
        if (pimpl->finished != nullptr) {
            wgpuCommandBufferRelease(pimpl->finished);
        }
        if (pimpl->encoder != nullptr) {
            wgpuCommandEncoderRelease(pimpl->encoder);
        }
    }

    CommandBuffer::CommandBuffer(CommandBuffer &&) noexcept = default;
    CommandBuffer &CommandBuffer::operator=(CommandBuffer &&) noexcept = default;

    CommandBufferHandle CommandBuffer::GetHandle() const noexcept {
        // Recording hands out the encoder; a finished buffer hands out itself. The seam
        // has one handle for both, and which one a caller needs is decided by whether it
        // has called End.
        if (pimpl->finished != nullptr) {
            return FromWgpuFinishedBuffer(pimpl->finished);
        }
        return FromWgpuCommandEncoder(pimpl->encoder);
    }

    void CommandBuffer::Begin() {
        // Creating the encoder began it. A second Begin would need a second encoder, and
        // nothing in the seam asks for one.
    }

    void CommandBuffer::End() {
        if (pimpl->encoder == nullptr || pimpl->finished != nullptr) {
            return;
        }
        pimpl->finished = wgpuCommandEncoderFinish(pimpl->encoder, nullptr);
        if (pimpl->finished == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: finishing a command encoder failed.");
        }
    }

    void CommandBuffer::Reset() {
        if (pimpl->finished != nullptr) {
            wgpuCommandBufferRelease(pimpl->finished);
            pimpl->finished = nullptr;
        }
        if (pimpl->encoder != nullptr) {
            wgpuCommandEncoderRelease(pimpl->encoder);
        }
        pimpl->encoder = wgpuDeviceCreateCommandEncoder(pimpl->device, nullptr);
    }
} // namespace Engine::Rhi

