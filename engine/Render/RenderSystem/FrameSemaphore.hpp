#ifndef RENDER_RENDERSYSTEM_FRAMESEMAPHORE_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMESEMAPHORE_INCLUDED

#include <cstdint>
#include <optional>
#include <utility>

#include "Rhi/Device/Semaphore.h"
#include "Rhi/RhiTypes.h"
#include "Render/render_export.h"

namespace Engine::RenderSystemState {
    /**
     * @brief Simple, frame-based wrapper around Vulkan timeline semaphore.
     */
    class RENDER_API FrameSemaphore {
        std::optional<Rhi::Semaphore> timeline_semaphore{};
        uint32_t current_frame_expected_timepoints{0};
        uint64_t total_elapsed_timepoints{0};

    public:
        void SetSemaphore(Rhi::Semaphore &&s) noexcept {
            timeline_semaphore = std::move(s);
        }
        Rhi::SemaphoreHandle GetSemaphore() const noexcept {
            return timeline_semaphore->GetHandle();
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

        /**
         * @brief A submission dependency waiting for this frame's timepoint.
         */
        Rhi::SubmissionDependency GetDependency(uint32_t timepoint) const noexcept {
            return Rhi::SubmissionDependency{GetSemaphore(), GetTimepointValue(timepoint), Rhi::PipelineScope::Unspecified};
        }
    };
} // namespace Engine::RenderSystemState

#endif // RENDER_RENDERSYSTEM_FRAMESEMAPHORE_INCLUDED

