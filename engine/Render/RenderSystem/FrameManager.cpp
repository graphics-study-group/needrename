#include "FrameManager.h"

#include "Render/Memory/Buffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/Swapchain.h"

#include <SDL3/SDL.h>

namespace Engine::RenderSystemState{
    FrameManager::FrameManager(RenderSystem &sys) : m_system(sys)
    {
    }

    void FrameManager::Create()
    {
        auto device = m_system.getDevice();

        vk::SemaphoreCreateInfo sinfo {};
        vk::FenceCreateInfo finfo {
            {vk::FenceCreateFlagBits::eSignaled}
        };
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            image_acquired_semaphores[i] = device.createSemaphoreUnique(sinfo);
            render_command_executed_semaphores[i] = device.createSemaphoreUnique(sinfo);
            copy_to_swapchain_completed_semaphores[i] = device.createSemaphoreUnique(sinfo);
            next_frame_ready_semaphores[i] = device.createSemaphoreUnique(sinfo);
            command_executed_fences[i] = device.createFenceUnique(finfo);
        }

        auto pool = m_system.getQueueInfo().graphicsPool.get();
        auto queue = m_system.getQueueInfo().graphicsQueue;

        vk::CommandBufferAllocateInfo cbinfo {
            pool, vk::CommandBufferLevel::ePrimary, FRAMES_IN_FLIGHT
        };
        auto new_command_buffers = device.allocateCommandBuffersUnique(cbinfo);

        render_command_buffers.clear();
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            command_buffers[i] = std::move(new_command_buffers[i]);
            render_command_buffers.emplace_back(
                m_system,
                command_buffers[i].get(), 
                queue, 
                command_executed_fences[i].get(),
                // Wait for the last
                next_frame_ready_semaphores[(i + FRAMES_IN_FLIGHT - 1) % FRAMES_IN_FLIGHT].get(),
                render_command_executed_semaphores[i].get(),
                i
            );
        }

        new_command_buffers = device.allocateCommandBuffersUnique(cbinfo);
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            copy_to_swapchain_command_buffers[i] = std::move(new_command_buffers[i]);
        }

        graphic_queue = queue;
        present_queue = m_system.getQueueInfo().presentQueue;
        swapchain = m_system.GetSwapchain().GetSwapchain();
        current_frame_in_flight = 0;

        m_submission_helper = std::make_unique <SubmissionHelper> (m_system);
    }

    uint32_t FrameManager::GetFrameInFlight() const noexcept
    {
        assert(this->current_frame_in_flight < FRAMES_IN_FLIGHT && "Frame Manager is in invalid state.");
        return this->current_frame_in_flight;
    }

    uint32_t FrameManager::GetFramebuffer() const noexcept
    {
        assert(this->current_framebuffer < std::numeric_limits<uint32_t>::max() && "Frame Manager is in invalid state.");
        return this->current_framebuffer;
    }

    RenderCommandBuffer & FrameManager::GetCommandBuffer()
    {
        assert(this->current_framebuffer < std::numeric_limits<uint32_t>::max() && "Frame Manager is in invalid state.");
        return render_command_buffers[GetFrameInFlight()];
    }

    std::vector<RenderCommandBuffer> &FrameManager::GetCommandBuffers()
    {
        return render_command_buffers;
    }

    uint32_t FrameManager::StartFrame(uint64_t timeout)
    {
        m_submission_helper->StartFrame();

        auto device = m_system.getDevice();
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

    void FrameManager::CopyToFrameBuffer(vk::Image image, vk::Extent2D extent, vk::Offset2D offsetSrc, vk::Offset2D offsetDst)
    {
        uint32_t fif = GetFrameInFlight();
        auto cb = copy_to_swapchain_command_buffers[fif].get();
        auto framebuffer_image = m_system.GetSwapchain().GetImages()[this->current_framebuffer];

        vk::CommandBufferBeginInfo cbbi {};
        cb.begin(cbbi);

        // Transit for transfer
        // We dont need barriers here since the semaphore ensures that rendering is completed
        std::array<vk::ImageMemoryBarrier2, 2> barriers = {
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eNone, // > Set up execution dep instead of memory dep.
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                framebuffer_image,
                vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
                }
            },
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eNone,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                image,
                vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
                }
            }
        };
        vk::DependencyInfo dep{
            {}, {}, {}, barriers
        };
        cb.pipelineBarrier2(dep);

        cb.copyImage(
            image,
            vk::ImageLayout::eTransferSrcOptimal,
            framebuffer_image,
            vk::ImageLayout::eTransferDstOptimal,
            {
                vk::ImageCopy{
                    vk::ImageSubresourceLayers{
                        vk::ImageAspectFlagBits::eColor,
                        0, 0, 1
                    },
                    vk::Offset3D{offsetSrc, 0},
                    vk::ImageSubresourceLayers{
                        vk::ImageAspectFlagBits::eColor,
                        0, 0, 1
                    },
                    vk::Offset3D{offsetDst, 0},
                    vk::Extent3D{extent, 1}
                }
            }
        );

        // Transit for presenting
        barriers = {
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eBottomOfPipe,
                vk::AccessFlagBits2::eNone,
                vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::ePresentSrcKHR,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                framebuffer_image,
                vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
                }
            },
            // The second barrier might be ignored.
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead,
                vk::PipelineStageFlagBits2::eBottomOfPipe,
                vk::AccessFlagBits2::eNone,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                image,
                vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
                }
            }
        };
        cb.pipelineBarrier2(dep);
        cb.end();

        // Wait for both command execution and image aquisition.
        std::array <vk::Semaphore, 2> rces = {
            render_command_executed_semaphores[fif].get(),
            image_acquired_semaphores[fif].get()
        };
        std::array <vk::PipelineStageFlags, 2> psfb = {
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer
        };

        // Signal ready for presenting and for next frame.
        std::array <vk::Semaphore, 2> ss = {
            copy_to_swapchain_completed_semaphores[fif].get(),
            next_frame_ready_semaphores[fif].get()
        };

        vk::SubmitInfo sinfo {
            rces,
            psfb,
            {cb},
            ss
        };
        graphic_queue.submit(sinfo);
    }

    void FrameManager::CopyToFramebuffer(vk::Image image)
    {
        CopyToFrameBuffer(image, m_system.GetSwapchain().GetExtent());
    }

    void FrameManager::CompleteFrame()
    {
        // Queue a present directive
        std::array<vk::SwapchainKHR, 1> swapchains { swapchain };
        std::array<uint32_t, 1> frame_indices { GetFramebuffer() };
        // Wait for command buffer before presenting the frame
        std::array<vk::Semaphore, 1> semaphores {copy_to_swapchain_completed_semaphores[GetFrameInFlight()].get()};
        vk::PresentInfoKHR info{semaphores, swapchains, frame_indices};
        vk::Result result = present_queue.presentKHR(info);
        if (result != vk::Result::eSuccess) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER, 
                "Presenting returned %s other than success.",
                vk::to_string(result).c_str()
                );
        }

        // Increment FIF counter, reset framebuffer index
        current_frame_in_flight = (current_frame_in_flight + 1) % FRAMES_IN_FLIGHT;
        current_framebuffer = std::numeric_limits<uint32_t>::max();

        // Handle submissions
        m_submission_helper->CompleteFrame();
    }
    SubmissionHelper &FrameManager::GetSubmissionHelper()
    {
        return *m_submission_helper;
    }
}
