#include "TwoStageSynch.h"

#include "Render/RenderSystem.h"

namespace Engine {
    TwoStageSynch::TwoStageSynch(const RenderSystem & system) : Synchronization(system) {
        const auto & device = system.getDevice();

        m_imageAvailable = device.createSemaphoreUnique({});
        m_renderFinished = device.createSemaphoreUnique({});
        m_inflight = device.createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
    }

    vk::Semaphore TwoStageSynch::GetNextImageSemaphore(uint32_t) const {
        return m_imageAvailable.get();
    }
    std::vector<vk::Semaphore> TwoStageSynch::GetCommandBufferWaitSignals(uint32_t) const {
        return {m_imageAvailable.get()};
    }
    std::vector<vk::PipelineStageFlags> TwoStageSynch::GetCommandBufferWaitSignalFlags(uint32_t) const {
        return {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    }
    std::vector<vk::Semaphore> TwoStageSynch::GetCommandBufferSigningSignals(uint32_t) const {
        return {m_renderFinished.get()};
    }
    vk::Fence TwoStageSynch::GetCommandBufferFence(uint32_t) const {
        return {m_inflight.get()};
    }
};
