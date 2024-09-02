#ifndef RENDER_PIPELINE_SYNCHRONIZATION_INFLIGHTTWOSTAGESYNCH_INCLUDED
#define RENDER_PIPELINE_SYNCHRONIZATION_INFLIGHTTWOSTAGESYNCH_INCLUDED

#include "Render/RenderSystem/Synch/Synchronization.h"

namespace Engine {
    class InFlightTwoStageSynch : public Synchronization
    {
    public:

        InFlightTwoStageSynch(const RenderSystem & system, uint32_t inflight_frame_count);
        virtual ~InFlightTwoStageSynch() = default;

        virtual vk::Semaphore GetNextImageSemaphore(uint32_t inflight) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferWaitSignals(uint32_t inflight) const;
        virtual std::vector<vk::PipelineStageFlags> GetCommandBufferWaitSignalFlags(uint32_t inflight) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferSigningSignals(uint32_t inflight) const;
        virtual vk::Fence GetCommandBufferFence(uint32_t inflight) const;

    protected:
        uint32_t m_inflight_frame_count;
        std::vector<vk::UniqueSemaphore> m_imageAvailable {};
        std::vector<vk::UniqueSemaphore> m_renderFinished {};
        std::vector<vk::UniqueFence> m_inflight {};
    };
};

#endif // RENDER_PIPELINE_SYNCHRONIZATION_INFLIGHTTWOSTAGESYNCH_INCLUDED
