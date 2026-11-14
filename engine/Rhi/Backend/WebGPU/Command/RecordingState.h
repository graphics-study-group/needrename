#ifndef ENGINE_RHI_BACKEND_WEBGPU_COMMAND_RECORDINGSTATE_INCLUDED
#define ENGINE_RHI_BACKEND_WEBGPU_COMMAND_RECORDINGSTATE_INCLUDED

#include <cstddef>
#include <cstdint>
#include <vector>
#include <webgpu/webgpu.h>

#include "Rhi/rhi_export.h"

namespace Engine::Rhi {
    /**
     * @brief Everything one command buffer knows while it is being recorded.
     *
     * Three units need to see the same recording at different moments: the
     * `CommandEncoder` that records into it, the `CommandBuffer` that finishes it
     * and the `DeviceContext` that publishes its constants just before submitting
     * it. Keeping one state object per command buffer is what makes those views
     * agree - a pass that is open when the recording is finished is a pass this
     * state can close, and the constant windows a submission must publish are the
     * windows this state staged.
     *
     * The state is owned by its `CommandBuffer` and reached from the native
     * encoder handle through the registry below. It is single-threaded, like the
     * rest of this backend.
     */
    struct RecordingState {
        /// @brief The encoder this state currently records into.
        WGPUCommandEncoder encoder{nullptr};
        /// @brief The finished command buffer, set by `CommandBuffer::End`.
        WGPUCommandBuffer finished{nullptr};
        /// @brief The open render pass, if any.
        WGPURenderPassEncoder render_pass{nullptr};
        /// @brief The open compute pass, if any. At most one of the two is non-null.
        WGPUComputePassEncoder compute_pass{nullptr};
        /// @brief Whether `render_pass` is open, as the interface spells it.
        bool in_render_pass{false};
        /// @brief Whether the open render pass has had a pipeline set in it.
        bool pass_pipeline_set{false};
        /**
         * @brief The constant windows this command buffer has recorded, back to back.
         *
         * Window `n` sits at device offset `n * k_constant_window`, and the whole
         * vector is what one queue write publishes before the submission. Recording
         * appends; submission publishes and recycles.
         */
        std::vector<std::byte> constant_staging{};
    };

    /**
     * @brief Create the recording state for an encoder and register it.
     *
     * @param encoder The encoder the state records into.
     * @return The new state, owned by the caller.
     */
    RHI_API RecordingState *CreateRecordingState(WGPUCommandEncoder encoder);

    /**
     * @brief Unregister and destroy a recording state.
     *
     * The owned passes are closed first, so a caller that destroys a half-recorded
     * command buffer leaves nothing dangling.
     */
    RHI_API void DestroyRecordingState(RecordingState *state) noexcept;

    /// @brief Find the state recording into `encoder`, or null when the handle is not one of ours.
    RHI_API RecordingState *FindRecordingState(WGPUCommandEncoder encoder) noexcept;

    /// @brief Find the state whose finished command buffer is `buffer`, or null.
    RHI_API RecordingState *FindFinishedRecordingState(WGPUCommandBuffer buffer) noexcept;

    /**
     * @brief Associate a finished command buffer with the state that produced it.
     *
     * The submission path receives finished buffers, so this is how it reaches the
     * staging a command buffer recorded.
     */
    RHI_API void RegisterFinishedCommandBuffer(RecordingState &state, WGPUCommandBuffer finished) noexcept;

    /// @brief Drop the finished-buffer association, without releasing the buffer.
    RHI_API void ForgetFinishedCommandBuffer(RecordingState &state) noexcept;

    /// @brief End and drop the open render pass, if there is one.
    RHI_API void CloseRenderPass(RecordingState &state) noexcept;

    /**
     * @brief End and drop the open compute pass, if there is one.
     *
     * The encoder opens this pass on the first compute pipeline bind and keeps it
     * open for consecutive compute commands; anything that cannot be recorded
     * inside it closes it here first.
     */
    RHI_API void CloseComputePass(RecordingState &state) noexcept;

    /// @brief Close whichever pass is open, render first.
    RHI_API void CloseOpenPass(RecordingState &state) noexcept;

    /**
     * @brief Re-point the state at a fresh encoder, discarding what it recorded.
     *
     * Called by `CommandBuffer::Reset`, which replaces the encoder object and must
     * leave the state as empty as a new command buffer's.
     */
    RHI_API void ResetRecordingState(RecordingState &state, WGPUCommandEncoder encoder) noexcept;

    /// @brief Whether this command buffer staged constant windows that are not published yet.
    RHI_API bool HasStagedConstants(const RecordingState &state) noexcept;

    /**
     * @brief Publish the staged windows with one queue write.
     *
     * The write covers the contiguous used range from device offset zero. Queue
     * ordering is what makes it safe: the write is ordered after every earlier
     * submission and before the submission that follows it.
     */
    RHI_API void PublishStagedConstants(RecordingState &state) noexcept;

    /// @brief Empty the staging so the command buffer can record another cycle.
    RHI_API void RecycleStagedConstants(RecordingState &state) noexcept;

    /**
     * @brief The host-visible buffer the constant windows live in.
     *
     * The bind group is bound with a dynamic offset, so one ring serves every
     * pipeline that declares a constant range.
     */
    struct ConstantRingRef {
        /// @brief The bind group holding the ring buffer at binding zero.
        WGPUBindGroup group{nullptr};
        /// @brief How many bytes of windows the ring holds.
        size_t capacity{0};

        /// @brief Whether the ring exists on this device.
        bool Ready() const noexcept {
            return group != nullptr && capacity != 0;
        }
    };

    /**
     * @brief Get the constant ring, creating it on first use.
     *
     * @return An empty reference when no device has been registered.
     */
    RHI_API ConstantRingRef GetConstantRing();

    /**
     * @brief Structural counters the tests and the performance note read.
     *
     * These count the work the recording path does, not how long it takes: a
     * submission's constant writes and compute passes are what the optimization
     * is supposed to change, and they are the same on every machine.
     */
    struct SubmissionCounters {
        /// @brief Compute passes opened. A shared scope makes this far smaller than the dispatch count.
        uint64_t compute_passes{0};
        /// @brief Compute dispatches recorded.
        uint64_t dispatches{0};
        /// @brief `wgpuQueueWriteBuffer` calls made to publish constant windows.
        uint64_t constant_writes{0};
        /// @brief Constant windows published.
        uint64_t constant_windows{0};
        /// @brief `wgpuQueueSubmit` calls made.
        uint64_t queue_submits{0};
    };

    /// @brief Read the counters.
    RHI_API const SubmissionCounters &GetSubmissionCounters() noexcept;
    /// @brief Zero the counters.
    RHI_API void ResetSubmissionCounters() noexcept;
    /// @brief Count one opened compute pass.
    RHI_API void CountComputePassOpened() noexcept;
    /// @brief Count one recorded compute dispatch.
    RHI_API void CountDispatch() noexcept;
    /// @brief Count one queue write that published `windows` constant windows.
    RHI_API void CountConstantWrite(uint64_t windows) noexcept;
    /// @brief Count one `wgpuQueueSubmit`.
    RHI_API void CountQueueSubmit() noexcept;
} // namespace Engine::Rhi

#endif // ENGINE_RHI_BACKEND_WEBGPU_COMMAND_RECORDINGSTATE_INCLUDED
