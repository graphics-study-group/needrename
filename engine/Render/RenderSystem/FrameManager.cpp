#include "FrameManager.h"

#include "Render/RenderSystem.h"

namespace Engine::RenderSystemState{

    void FrameManager::Create(std::shared_ptr <RenderSystem> system)
    {
        m_system = system;
        auto device = system->getDevice();

        vk::SemaphoreCreateInfo sinfo {};
        vk::FenceCreateInfo finfo {
            {vk::FenceCreateFlagBits::eSignaled}
        };
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            image_acquired_semaphores[i] = device.createSemaphoreUnique(sinfo);
            command_executed_semaphores[i] = device.createSemaphoreUnique(sinfo);
            command_executed_fences[i] = device.createFenceUnique(finfo);
        }

        auto pool = system->getQueueInfo().graphicsPool.get();
        auto queue = system->getQueueInfo().graphicsQueue;

        vk::CommandBufferAllocateInfo cbinfo {
            pool, vk::CommandBufferLevel::ePrimary, FRAMES_IN_FLIGHT
        };
        auto new_command_buffers = device.allocateCommandBuffersUnique(cbinfo);

        render_command_buffers.clear();
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            command_buffers[i] = std::move(new_command_buffers[i]);
            render_command_buffers.emplace_back(system);
            render_command_buffers[i].SetCommandBuffer(
                command_buffers[i].get(), 
                queue, 
                command_executed_fences[i].get(),
                image_acquired_semaphores[i].get(),
                command_executed_semaphores[i].get(),
                i
            );
        }

        present_queue = system->getQueueInfo().presentQueue;
        swapchain = system->GetSwapchain().GetSwapchain();
        current_frame_in_flight = 0;
    }

    uint32_t FrameManager::GetFrameInFlight()
    {
        assert(this->current_frame_in_flight < FRAMES_IN_FLIGHT && "Frame Manager is in invalid state.");
        return this->current_frame_in_flight;
    }

    uint32_t FrameManager::GetFramebuffer()
    {
        assert(this->current_framebuffer < std::numeric_limits<uint32_t>::max() && "Frame Manager is in invalid state.");
        return this->current_framebuffer;
    }

    RenderCommandBuffer & FrameManager::GetCommandBuffer()
    {
        return render_command_buffers[GetFrameInFlight()];
    }

    std::vector<RenderCommandBuffer> &FrameManager::GetCommandBuffers()
    {
        return render_command_buffers;
    }

    uint32_t FrameManager::StartFrame(uint64_t timeout)
    {
        auto system = m_system.lock();
        auto device = system->getDevice();
        uint32_t fif = GetFrameInFlight();

        // Wait for command buffer execution.
        vk::Fence fence = command_executed_fences[fif].get();
        vk::Result wait_result = device.waitForFences({fence}, vk::True, timeout);
        if (wait_result == vk::Result::eTimeout) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Timed out waiting for fence for frame id %u.", fif);
        }
        render_command_buffers[fif].Reset();
        device.resetFences({fence});

        // Acquire new image
        auto acquire_result = device.acquireNextImageKHR(
            swapchain, 
            timeout, 
            image_acquired_semaphores[fif].get(),
            nullptr
        );
        if (acquire_result.result == vk::Result::eTimeout) {
            SDL_LogError(0, "Timed out waiting for next frame.");
            return -1;
        } else if (acquire_result.result != vk::Result::eSuccess) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER, 
                "AcquireNextImage returned %s other than success.",
                vk::to_string(acquire_result.result).c_str()
                );
        }
        current_framebuffer = acquire_result.value;
        return current_framebuffer;
    }

    void FrameManager::CompleteFrame()
    {
        // Queue a present directive
        std::array<vk::SwapchainKHR, 1> swapchains { swapchain };
        std::array<uint32_t, 1> frame_indices { GetFramebuffer() };
        std::array<vk::Semaphore, 1> semaphores {command_executed_semaphores[GetFrameInFlight()].get()};
        vk::PresentInfoKHR info{semaphores, swapchains, frame_indices};
        vk::Result result = present_queue.presentKHR(info);
        if (result != vk::Result::eSuccess) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER, 
                "Presenting returned %s other than success.",
                vk::to_string(result).c_str()
                );
        }

        // Increment FIF counter
        current_frame_in_flight = (current_frame_in_flight + 1) % FRAMES_IN_FLIGHT;
        current_framebuffer = std::numeric_limits<uint32_t>::max();
    }
}
