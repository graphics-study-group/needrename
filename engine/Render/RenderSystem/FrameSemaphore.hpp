#ifndef RENDER_RENDERSYSTEM_FRAMESEMAPHORE_INCLUDED
#define RENDER_RENDERSYSTEM_FRAMESEMAPHORE_INCLUDED

#include <vulkan/vulkan.hpp>
#include <cstdint>

namespace Engine::RenderSystemState {
    struct FrameSemaphore final {
        vk::UniqueSemaphore timeline_semaphore;
        uint64_t frame_count;

        enum class TimePoint {
            Pending,
            PreTransferFinished,
            PreComputeFinished,
            // XXX: investigate possible optimization opportunity.
            // maybe we should split GUI drawing from main graphics and run GUI in parallel with post compute.
            // (See https://docs.vulkan.org/samples/latest/samples/performance/async_compute/README.html)
            // in that case the post compute finished semaphore should be renamed to `GUIFinished`,
            // as dependency in the same async compute queue can be expressed by barriers instead of semaphores.
            // Or we can run post-process compute in main graphics queue and leave post compute for composition only.
            GraphicFinished,
            PostComputeFinished,
            // While `VkPresentInfoKHR` only accepts binary semaphore, we still need this to prevent writing on textures being copied to present.
            CopyToPresentFinished,
            MAX_TIME_POINTS
        };

        void StepFrame() {
            frame_count++;
        }

        uint64_t GetTimepointValue(TimePoint timepoint) const noexcept {
            return frame_count * std::underlying_type_t<TimePoint>(TimePoint::MAX_TIME_POINTS) 
                    + std::underlying_type_t<TimePoint>(timepoint);
        }

        vk::SemaphoreSubmitInfo GetSubmitInfo(TimePoint timepoint, vk::PipelineStageFlags2 stage) const noexcept {
            assert(timeline_semaphore);
            return vk::SemaphoreSubmitInfo{
                timeline_semaphore.get(), 
                GetTimepointValue(timepoint),
                stage
            };
        }
    };
}

#endif // RENDER_RENDERSYSTEM_FRAMESEMAPHORE_INCLUDED
