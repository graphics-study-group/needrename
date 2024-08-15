#include "Synchronization.h"
#include "Render/RenderSystem.h"
#include <SDL3/SDL.h>

namespace Engine
{
    Synchronization::Synchronization(const RenderSystem & system) : m_system(system) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Creating synchronization primitives.");
    }

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

    InFlightTwoStageSynchronization::InFlightTwoStageSynchronization(const RenderSystem& system) : Synchronization(system) {
        const auto & device = system.getDevice();
        for (uint32_t i = 0; i < INFLIGHT_SIZE; i++) {
            m_imageAvailable[i] = device.createSemaphoreUnique({});
            m_renderFinished[i] = device.createSemaphoreUnique({});
            m_inflight[i] = device.createFenceUnique({vk::FenceCreateFlagBits::eSignaled});
        }
    }

    vk::Semaphore InFlightTwoStageSynchronization::GetNextImageSemaphore(uint32_t inflight) const {
        assert(inflight < INFLIGHT_SIZE);
        return m_imageAvailable[inflight].get();
    }
    std::vector<vk::Semaphore> 
    InFlightTwoStageSynchronization::GetCommandBufferWaitSignals(uint32_t inflight) const {
        assert(inflight < INFLIGHT_SIZE);
        return {m_imageAvailable[inflight].get()};
    }
    std::vector<vk::PipelineStageFlags> 
    InFlightTwoStageSynchronization::GetCommandBufferWaitSignalFlags(uint32_t inflight) const {
        assert(inflight < INFLIGHT_SIZE);
        return {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    }
    std::vector<vk::Semaphore> 
    InFlightTwoStageSynchronization::GetCommandBufferSigningSignals(uint32_t inflight) const {
        assert(inflight < INFLIGHT_SIZE);
        return {m_renderFinished[inflight].get()};
    }
    vk::Fence 
    InFlightTwoStageSynchronization::GetCommandBufferFence(uint32_t inflight) const {
        assert(inflight < INFLIGHT_SIZE);
        return {m_inflight[inflight].get()};
    }
} // namespace Engine


