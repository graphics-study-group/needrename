#include "TwoStageSynchronization.h"

#include "Render/RenderSystem.h"

namespace Engine {
    TwoStageSynchronization::TwoStageSynchronization(const RenderSystem & system) : Synchronization(system) {
        const auto & device = system.getDevice();

        m_imageAvailable = device.createSemaphoreUnique({});
        m_renderFinished = device.createSemaphoreUnique({});
        m_inflight = device.createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
    }

    vk::Semaphore TwoStageSynchronization::GetNextImageSemaphore(uint32_t) const {
        return m_imageAvailable.get();
    }
    std::vector<vk::Semaphore> TwoStageSynchronization::GetCommandBufferWaitSignals(uint32_t) const {
        return {m_imageAvailable.get()};
    }
    std::vector<vk::PipelineStageFlags> TwoStageSynchronization::GetCommandBufferWaitSignalFlags(uint32_t) const {
        return {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    }
    std::vector<vk::Semaphore> TwoStageSynchronization::GetCommandBufferSigningSignals(uint32_t) const {
        return {m_renderFinished.get()};
    }
    vk::Fence TwoStageSynchronization::GetCommandBufferFence(uint32_t) const {
        return {m_inflight.get()};
    }
};
