#include "Rhi/Command/RecordingScope.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Command/CommandBuffer.h"
#include "Rhi/Command/CommandEncoder.h"
#include "Rhi/RhiTypes.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    /**
     * @brief A one-shot recording that submits itself.
     *
     * The Vulkan version allocates a command buffer from a one-time pool; here it creates
     * an encoder, and `Submit` finishes it and hands it to the queue. `on_complete`
     * becomes the queue's own completion callback rather than a fence the caller waits
     * on, which is the shape this backend gives every submission.
     *
     * An abandoned scope still finishes its encoder before the destructor releases it,
     * for the same reason as on Vulkan: an encoder with work recorded but never finished
     * is a leak whose size depends on what was recorded.
     */
    struct RecordingScope::impl {
        DeviceContext *context{nullptr};
        std::unique_ptr<CommandBuffer> buffer{};
        std::unique_ptr<CommandEncoder> encoder{};
        bool submitted{false};
        std::function<void()> on_complete{};
    };

    namespace {
        void OnQueueWorkDone(WGPUQueueWorkDoneStatus status, WGPUStringView, void *, void *) {
            if (status != WGPUQueueWorkDoneStatus_Success) {
                SDL_LogError(
                    SDL_LOG_CATEGORY_RENDER,
                    "WebGPU: a submission did not complete successfully (status %d).",
                    static_cast<int>(status)
                );
            }
        }

        void OnQueueWorkDoneThenRun(
            WGPUQueueWorkDoneStatus status, WGPUStringView message, void *userdata1, void *
        ) {
            auto *callback = static_cast<std::function<void()> *>(userdata1);
            if (status != WGPUQueueWorkDoneStatus_Success) {
                SDL_LogError(
                    SDL_LOG_CATEGORY_RENDER,
                    "WebGPU: a submission did not complete successfully (status %d): %.*s",
                    static_cast<int>(status),
                    static_cast<int>(message.length),
                    message.data != nullptr ? message.data : ""
                );
            }
            if (callback != nullptr && *callback) {
                (*callback)();
            }
        }
    } // namespace

    RecordingScope::RecordingScope(DeviceContext &context) : pimpl(std::make_unique<impl>()) {
        pimpl->context = &context;
        auto buffers = CommandBuffer::Create(context, CommandBufferPool::GraphicsOneTime, 1);
        pimpl->buffer = std::make_unique<CommandBuffer>(std::move(buffers[0]));
        pimpl->encoder = std::make_unique<CommandEncoder>(pimpl->buffer->GetHandle());
    }

    RecordingScope::~RecordingScope() {
        if (!pimpl->submitted && pimpl->buffer) {
            // Close the recording rather than leave it open; see the class comment.
            pimpl->buffer->End();
        }
    }

    CommandEncoder &RecordingScope::Encoder() noexcept {
        return *pimpl->encoder;
    }

    void RecordingScope::Submit() {
        if (pimpl->submitted) {
            return;
        }
        pimpl->submitted = true;

    }
} // namespace Engine::Rhi




