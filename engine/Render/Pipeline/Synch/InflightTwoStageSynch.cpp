#include "InflightTwoStageSynch.h"

#include "Render/RenderSystem.h"

namespace Engine{
    InFlightTwoStageSynch::InFlightTwoStageSynch(const RenderSystem& system) : Synchronization(system) {
        const auto & device = system.getDevice();
        for (uint32_t i = 0; i < INFLIGHT_SIZE; i++) {
            m_imageAvailable[i] = device.createSemaphoreUnique({});
            m_renderFinished[i] = device.createSemaphoreUnique({});
            m_inflight[i] = device.createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
        }
    }

    vk::Semaphore InFlightTwoStageSynch::GetNextImageSemaphore(uint32_t inflight) const {
        assert(inflight < INFLIGHT_SIZE);
        return m_imageAvailable[inflight].get();
    }
    std::vector<vk::Semaphore> 
    InFlightTwoStageSynch::GetCommandBufferWaitSignals(uint32_t inflight) const {
        assert(inflight < INFLIGHT_SIZE);
        return {m_imageAvailable[inflight].get()};
    }
    std::vector<vk::PipelineStageFlags> 
    InFlightTwoStageSynch::GetCommandBufferWaitSignalFlags(uint32_t inflight) const {
        assert(inflight < INFLIGHT_SIZE);
        return {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    }
    std::vector<vk::Semaphore> 
    InFlightTwoStageSynch::GetCommandBufferSigningSignals(uint32_t inflight) const {
        assert(inflight < INFLIGHT_SIZE);
        return {m_renderFinished[inflight].get()};
    }
    vk::Fence 
    InFlightTwoStageSynch::GetCommandBufferFence(uint32_t inflight) const {
        assert(inflight < INFLIGHT_SIZE);
        return {m_inflight[inflight].get()};
    }
}
