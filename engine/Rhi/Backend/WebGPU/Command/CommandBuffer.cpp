#include "Rhi/Command/CommandBuffer.h"

#include "Rhi/Backend/WebGPU/Command/RecordingState.h"
#include "Rhi/Backend/WebGPU/WgpuHandles.h"

namespace Engine::Rhi {
    /// @brief Release the staging buffers a finished command buffer recorded against.
    /// @note Defined in the command encoder unit, which owns the list.
    void ReleaseRetiredStagingBuffers(WGPUCommandEncoder encoder);
} // namespace Engine::Rhi
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
     *
     * What the recording accumulated - the open compute scope and the staged constant
     * windows - lives in the shared `RecordingState` this object owns rather than in the
     * encoder object, because finishing the recording and publishing its constants both
     * happen outside the encoder. See `RecordingState.h`.
     */
    struct CommandBuffer::impl {
        WGPUDevice device{nullptr};
        /// Owned here and reachable from the native encoder handle; see `RecordingState.h`.
        std::unique_ptr<RecordingState> recording{};

        WGPUCommandEncoder Encoder() const noexcept {
            return recording != nullptr ? recording->encoder : nullptr;
        }

        WGPUCommandBuffer Finished() const noexcept {
            return recording != nullptr ? recording->finished : nullptr;
        }
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
            const WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
            // The state is created with the encoder so a command encoder built over this
            // handle in the same breath finds it; a recording with no state would have to
            // open its own and could not be finished or published coherently.
            state->recording.reset(CreateRecordingState(encoder));
            buffers.push_back(CommandBuffer(std::move(state)));
        }
        return buffers;
    }

    CommandBuffer::~CommandBuffer() {
        if (!pimpl) {
            return;
        }
        const WGPUCommandEncoder encoder = pimpl->Encoder();
        // A pass left open belongs to this recording; closing it here keeps the release
        // order sane - the encoder has to be alive while its pass is ended.
        DestroyRecordingState(pimpl->recording.release());
        if (encoder != nullptr) {
            // The frame manager destroys a command buffer only after the fence that proves its
            // commands have run, so this is where a read-back's staging buffer can be released.
            ReleaseRetiredStagingBuffers(encoder);
            wgpuCommandEncoderRelease(encoder);
        }
    }

    CommandBuffer::CommandBuffer(CommandBuffer &&) noexcept = default;
    CommandBuffer &CommandBuffer::operator=(CommandBuffer &&) noexcept = default;

    CommandBufferHandle CommandBuffer::GetHandle() const noexcept {
        // Recording hands out the encoder; a finished buffer hands out itself. The seam
        // has one handle for both, and which one a caller needs is decided by whether it
        // has called End.
        if (pimpl->Finished() != nullptr) {
            return FromWgpuFinishedBuffer(pimpl->Finished());
        }
        return FromWgpuCommandEncoder(pimpl->Encoder());
    }

    void CommandBuffer::Begin() {
        // Creating the encoder began it. A second Begin would need a second encoder, and
        // nothing in the seam asks for one.
    }

    void CommandBuffer::End() {
        if (pimpl->recording == nullptr || pimpl->Encoder() == nullptr || pimpl->Finished() != nullptr) {
            return;
        }
        // An open pass belongs to this recording, and finishing an encoder while a pass is
        // still open fails. Closing it here is why the recording state is shared rather than
        // living in the encoder: a caller that dispatches no longer has to close the scope
        // itself, and a compute scope deliberately left open for the next dispatch is closed
        // at exactly this boundary.
        CloseOpenPass(*pimpl->recording);
        WGPUCommandBuffer finished = wgpuCommandEncoderFinish(pimpl->Encoder(), nullptr);
        if (finished == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: finishing a command encoder failed.");
            return;
        }
        // Registered so `DeviceContext::Submit` can find this recording's staged constants
        // from the finished handle it is handed.
        RegisterFinishedCommandBuffer(*pimpl->recording, finished);
    }

    void CommandBuffer::Reset() {
        if (pimpl->recording == nullptr) {
            return;
        }
        const WGPUCommandBuffer finished = pimpl->Finished();
        if (finished != nullptr) {
            ForgetFinishedCommandBuffer(*pimpl->recording);
            wgpuCommandBufferRelease(finished);
        }
        // The open pass belongs to the encoder being replaced, so it is ended while that
        // encoder is still alive.
        CloseOpenPass(*pimpl->recording);
        const WGPUCommandEncoder previous = pimpl->Encoder();
        if (previous != nullptr) {
            wgpuCommandEncoderRelease(previous);
        }
        const WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(pimpl->device, nullptr);
        // Same state, new encoder: the staged windows and the pass are what a recording
        // cycle accumulates, and a reset starts a new cycle.
        ResetRecordingState(*pimpl->recording, encoder);
    }
} // namespace Engine::Rhi
