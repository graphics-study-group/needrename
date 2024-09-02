#ifndef RENDER_PIPELINE_SYNCHRONIZATION_INFLIGHTTWOSTAGESYNCH_INCLUDED
#define RENDER_PIPELINE_SYNCHRONIZATION_INFLIGHTTWOSTAGESYNCH_INCLUDED

#include "Render/Pipeline/Synch/Synchronization.h"

namespace Engine {
    class InFlightTwoStageSynch : public Synchronization
    {
    public:

        InFlightTwoStageSynch(const RenderSystem & system);
        virtual ~InFlightTwoStageSynch() = default;

        virtual vk::Semaphore GetNextImageSemaphore(uint32_t inflight) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferWaitSignals(uint32_t inflight) const;
        virtual std::vector<vk::PipelineStageFlags> GetCommandBufferWaitSignalFlags(uint32_t inflight) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferSigningSignals(uint32_t inflight) const;
        virtual vk::Fence GetCommandBufferFence(uint32_t inflight) const;

    protected:
        static constexpr uint32_t INFLIGHT_SIZE = 3;
        std::array<vk::UniqueSemaphore, INFLIGHT_SIZE> m_imageAvailable {};
        std::array<vk::UniqueSemaphore, INFLIGHT_SIZE> m_renderFinished {};
        std::array<vk::UniqueFence, INFLIGHT_SIZE> m_inflight {};
    };
};

#endif // RENDER_PIPELINE_SYNCHRONIZATION_INFLIGHTTWOSTAGESYNCH_INCLUDED
