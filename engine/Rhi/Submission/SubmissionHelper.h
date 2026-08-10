#ifndef ENGINE_RHI_SUBMISSIONHELPER_INCLUDED
#define ENGINE_RHI_SUBMISSIONHELPER_INCLUDED

#include "Rhi/rhi_export.h"

#include <vulkan/vulkan.hpp>

#include <functional>
#include <queue>
#include <span>

namespace Engine::Rhi {
    class Texture;
    class DeviceBuffer;

    class DeviceInterface;
    class AllocatorState;

    /**
     * @brief A helper for submitting data to GPU.
     *
     * Enqueued operations are executed in batches. A batch starts at a
     * submission call (`ExecuteSubmission` / `ExecuteSubmissionImmediately`)
     * and covers all operations enqueued since the previous submission
     * (or since construction). Enqueued operations keep their staging
     * buffers alive until the batch that carries them has finished
     * executing on the GPU.
     *
     * Batch state machine:
     * - `Reset`: no deferred batch is pending; submission is allowed.
     * - `Submitted`: a deferred batch (submitted via `ExecuteSubmission`)
     *   is pending and awaits `OnBatchComplete`.
     *
     * Protocol:
     * - `ExecuteSubmission` / `ExecuteSubmissionImmediately` require the
     *   state to be `Reset`, otherwise they throw `std::runtime_error`.
     * - `OnBatchComplete` reaps the pending deferred batch. It throws
     *   `std::runtime_error` when called with unsubmitted operations
     *   pending (enqueue operations must be submitted before the next
     *   `OnBatchComplete`; recording-phase enqueues still precede
     *   `ExecuteSubmission` in the caller's loop). Readback callbacks
     *   must not perform uploads.
     *
     * - `ExecuteSubmissionImmediately` is self-contained: it submits and
     *   waits for completion, leaving the state at `Reset`. It submits
     *   all currently pending operations.
     * - An empty `ExecuteSubmission` advances the caller-provided signal
     *   CPU-side without submitting anything.
     *
     * This class is NOT thread-safe. All methods must be called from a
     * single thread (the frame loop thread).
     */
    class RHI_API SubmissionHelper {
        using CmdOperation = std::function<void(vk::CommandBuffer)>;

    private:
        const DeviceInterface &m_device_interface;
        const AllocatorState &m_allocator;
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        SubmissionHelper(const DeviceInterface &device_interface, const AllocatorState &allocator);
        ~SubmissionHelper();

        SubmissionHelper(const SubmissionHelper &) = delete;
        SubmissionHelper &operator=(const SubmissionHelper &) = delete;

        /**
         * @brief Enqueue a buffer uploading.
         *
         * @param buffer buffer to be uploaded
         * @param data Host-side buffer containing all data.
         * These data are immediately copied to a staging buffer, and can
         * be freed after the invocation.
         */
        void EnqueueBufferSubmission(
            const DeviceBuffer &buffer, std::span<const std::byte> data, size_t buffer_offset = 0
        );

        /**
         * @brief Enqueue a texture buffer submission. Record corresponding image
         * barriers and buffer writes to a disposable command buffer.
         *
         * A staging buffer is created, and will be de-allocated when the batch
         * that carries it finishes.
         * The layout of the image will be transferred to optimal for shader read after submission.
         *
         * Only color aspect and the very first level of mipmap is considered for submission,
         * and no blitting or mipmap generation is recorded, which means that the data must
         * cover the whole image size and all array layers.
         *
         * @param texture
         * @param data
         * Linearized buffer data. Refer to
         * https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#copies-buffers-images-addressing
         * for how to organize the data.
         * Data are immediately copied to a staging buffer.
         * It is safe to free this buffer after calling this method.
         * @param length
         */
        void EnqueueTextureBufferSubmission(const Texture &texture, std::span<const std::byte> data);

        /**
         * @brief Enqueue a texture clear operation.
         * Record corresponding image barriers to a disposable command buffer, and issue a clear
         * operation. The layout of the image will be transferred to optimal for shader read
         * after clear operation.
         *
         * Useful for creating a blank default texture.
         * Only color aspect is cleared. All mipmap levels and arrays are cleared.
         *
         * @param texture texture to be cleared
         * @param color_rgba clear color
         */
        void EnqueueTextureClear(const Texture &texture, std::tuple<float, float, float, float> color_rgba);

        /**
         * @brief Enqueue a texture clear operation.
         * Record corresponding image barriers to a disposable command buffer, and issue a clear
         * operation. The layout of the image will be transferred to optimal for shader read
         * after clear operation.
         *
         * Useful for creating a blank default texture.
         * Only depth aspect is cleared. All mipmap levels and arrays are cleared.
         *
         * @param texture texture to be cleared
         * @param depth clear depth
         */
        void EnqueueTextureClear(const Texture &texture, float depth);
        // void EnqueueTextureClear(const Texture &texture, std::tuple<float, uint8_t> depth_stencil);

        /**
         * @brief Execute staged submissions as a deferred batch.
         *
         * Allocates a new command buffer if needed, records all pending operations, and
         * submits the buffer to the graphics queue with the provided signal.
         *
         * If no operations are pending, the provided signal is advanced CPU-side
         * (semaphore + value) and nothing is submitted.
         *
         * @param signal_info Batch completion signal: {semaphore, value} signaled
         * when all recorded operations have finished. Used by the caller to gate
         * dependent work (e.g. the main command buffer waiting on it).
         *
         * @warning The batch state must be `Reset`; otherwise a previous deferred
         * batch is still pending and this call throws `std::runtime_error`.
         * The batch is reaped by `OnBatchComplete`.
         */
        void ExecuteSubmission(vk::SemaphoreSignalInfo signal_info);

        /**
         * @brief Immediately execute staged submissions.
         *
         * Submits all currently pending operations, waits for completion on the
         * CPU, and releases their staging buffers. The batch state remains `Reset`.
         *
         * Use this method sparingly, as it causes CPU to stall and wait for all submission.
         *
         * @warning The batch state must be `Reset`; calling this while a deferred
         * batch is pending throws `std::runtime_error`.
         */
        void ExecuteSubmissionImmediately();

        /**
         * @brief Complete the frame.
         *
         * In the `Submitted` state: waits for execution of the deferred batch,
         * de-allocates its staging buffers, resets the fence, and reclaims the
         * command buffer, then returns to `Reset`.
         *
         * In the `Reset` state: returns idempotently when idle, or throws
         * `std::runtime_error` when unsubmitted operations are pending.
         */
        void OnBatchComplete();
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_SUBMISSIONHELPER_INCLUDED
