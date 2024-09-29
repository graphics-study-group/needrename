#include "InflightTwoStageSynch.h"

#include "Render/RenderSystem.h"

namespace Engine{
    InFlightTwoStageSynch::InFlightTwoStageSynch(
        const RenderSystem& system, 
        uint32_t inflight_frame_count
    ) : Synchronization(system), m_inflight_frame_count(inflight_frame_count) {
        const auto & device = system.getDevice();
        m_imageAvailable.resize(m_inflight_frame_count);
        m_renderFinished.resize(m_inflight_frame_count);
        m_inflight.resize(m_inflight_frame_count);
        for (uint32_t i = 0; i < m_inflight_frame_count; i++) {
            m_imageAvailable[i] = device.createSemaphoreUnique({});
            m_renderFinished[i] = device.createSemaphoreUnique({});
            m_inflight[i] = device.createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
        }
    }

    vk::Semaphore InFlightTwoStageSynch::GetNextImageSemaphore(uint32_t inflight) const {
        assert(inflight < m_inflight_frame_count);
        return m_imageAvailable[inflight].get();
    }
    std::vector<vk::Semaphore> 
    InFlightTwoStageSynch::GetCommandBufferWaitSignals(uint32_t inflight) const {
        assert(inflight < m_inflight_frame_count);
        return {m_imageAvailable[inflight].get()};
    }
    std::vector<vk::PipelineStageFlags> 
    InFlightTwoStageSynch::GetCommandBufferWaitSignalFlags(uint32_t inflight) const {
        assert(inflight < m_inflight_frame_count);
        return {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    }
    std::vector<vk::Semaphore> 
    InFlightTwoStageSynch::GetCommandBufferSigningSignals(uint32_t inflight) const {
        assert(inflight < m_inflight_frame_count);
        return {m_renderFinished[inflight].get()};
    }
    vk::Fence 
    InFlightTwoStageSynch::GetCommandBufferFence(uint32_t inflight) const {
        assert(inflight < m_inflight_frame_count);
        return {m_inflight[inflight].get()};
    }
}
