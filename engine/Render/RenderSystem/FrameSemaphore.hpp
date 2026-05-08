#ifndef RENDER_RENDERSYSTEM_FRAMESEMAPHORE_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMESEMAPHORE_INCLUDED

#include <cstdint>
#include <vulkan/vulkan.hpp>

namespace Engine::RenderSystemState {
    /**
     * @brief Simple, frame-based wrapper around Vulkan timeline semaphore.
     */
    class FrameSemaphore {
        vk::UniqueSemaphore timeline_semaphore{nullptr};
        uint32_t current_frame_expected_timepoints{0};
        uint64_t total_elapsed_timepoints{0};

    public:
        void SetSemaphore(vk::UniqueSemaphore &&s) noexcept {
            timeline_semaphore = std::move(s);
        }
        vk::Semaphore GetSemaphore() const noexcept {
            return timeline_semaphore.get();
        }
        uint32_t GetExpectedTimepoints() const noexcept {
            return current_frame_expected_timepoints;
        }
        uint64_t GetTotalElapsedTimepoints() const noexcept {
            return total_elapsed_timepoints;
        }

        /**
         * @brief Step the frame by adding expected timepoints to the elapsed
         * counter.
         *
         * The expected timepoints of the previous frame is not zeroed. You can
         * use `GetExpectedTimepoints()` to query this value until the next call
         * of `SetExpectedTimepoints()`.
         */
        void EndFrame() noexcept {
            assert(current_frame_expected_timepoints > 0 && "Expected timepoints of the current frame is not set yet.");
            total_elapsed_timepoints += current_frame_expected_timepoints;
        }

        /**
         * @brief Set the expected synchronization timepoint of the current
         * frame.
         *
         * Depending on the render process of the current frame, multiple
         * synchronization timepoints might be necessary. Typically, following
         * procedures needs its timepoint:
         *
         * - Start of the frame;
         * - Submission of host data to device;
         * - Pre-rendering compute, if it is distributed to another queue;
         * - Rendering passes;
         * - Post-processing compute, if it is distributed to another queue;
         * - Presenting.
         *
         * Start of the frame is needed to kickstart the frame without the
         * validation layer complaining.
         * Synchronization within rendering passes are managed by barriers.
         * Therefore, no timepoints are needed.
         */
        void SetExpectedTimepoints(uint32_t timepoints) {
            assert(timepoints > 0 && "Timepoints of any frame should be greater than zero.");
            current_frame_expected_timepoints = timepoints;
        }

        uint64_t GetTimepointValue(uint32_t timepoint) const noexcept {
            assert(timepoint <= current_frame_expected_timepoints && "Timepoint out of range.");
            return total_elapsed_timepoints + timepoint;
        }

        vk::SemaphoreSubmitInfo GetSubmitInfo(uint32_t timepoint, vk::PipelineStageFlags2 stage) const noexcept {
            assert(timeline_semaphore);
            assert(timepoint <= current_frame_expected_timepoints && "Timepoint out of range.");
            return vk::SemaphoreSubmitInfo{timeline_semaphore.get(), GetTimepointValue(timepoint), stage};
        }

        vk::SemaphoreSignalInfo GetSignalInfo(uint32_t timepoint) const noexcept {
            assert(timeline_semaphore);
            assert(timepoint <= current_frame_expected_timepoints && "Timepoint out of range.");
            return vk::SemaphoreSignalInfo{timeline_semaphore.get(), GetTimepointValue(timepoint)};
        }
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RENDERSYSTEM_FRAMESEMAPHORE_INCLUDED
