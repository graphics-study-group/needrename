#ifndef RENDER_PIPELINE_SYNCHRONIZATION_TWOSTAGESYNCHRONIZATION_INCLUDED
#define RENDER_PIPELINE_SYNCHRONIZATION_TWOSTAGESYNCHRONIZATION_INCLUDED

#include "Render/Pipeline/Synch/Synchronization.h"

namespace Engine {
    /// @brief A synchronization class with only three synchronization primitives:
    /// - a semaphore signaling ready to draw
    /// - a semaphore signaling finished
    /// - a fence for previous frame
    class TwoStageSynch : public Synchronization
    {
    public:

        TwoStageSynch(const RenderSystem & system);
        virtual ~TwoStageSynch() = default;

        virtual vk::Semaphore GetNextImageSemaphore(uint32_t frame) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferWaitSignals(uint32_t frame) const;
        virtual std::vector<vk::PipelineStageFlags> GetCommandBufferWaitSignalFlags(uint32_t frame) const;
        virtual std::vector<vk::Semaphore> GetCommandBufferSigningSignals(uint32_t frame) const;
        virtual vk::Fence GetCommandBufferFence(uint32_t frame) const;

    protected:
        vk::UniqueSemaphore m_imageAvailable {};
        vk::UniqueSemaphore m_renderFinished {};
        vk::UniqueFence m_inflight {};
    };
}

#endif // RENDER_PIPELINE_SYNCHRONIZATION_TWOSTAGESYNCHRONIZATION_INCLUDED
