#include "FrameManager.h"

#include "Render/Memory/Buffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/DebugUtils.h"

#include <SDL3/SDL.h>

namespace Engine::RenderSystemState{
    struct FrameManager::PresentingHelper {
        struct PresentingOperation {
            vk::Image image;
            vk::Extent2D extent;
            vk::Offset2D offset_src, offset_dst;
        };

        std::vector <PresentingOperation> operations {};

        /**
         * @brief Record the command buffer according to the saved operations.
         * The operations saved in the struct are left untouched.
         * 
         * `vkBeginCommandBuffer` and `vkEndCommandBuffer` are called on the buffer in this method, and therefore
         * the buffer is expected to be in Pending state when called, and will be in Executable state after calling.
         * 
         * The method transits the image to Transfer Source Layout and the dst to Transfer Destination Layout,
         * record a image copy command (not blitting command, so resizing is not possible), and transits the image
         * back to Color Attachment Optimal layout.
         */
        void RecordCopyCommand(const vk::CommandBuffer & cb, const vk::Image & dst, bool is_framebuffer = true) const {
            // We can cache this vector to further speed up recording.
            std::vector <vk::ImageMemoryBarrier2> barriers(operations.size() + 1, vk::ImageMemoryBarrier2{});
            DEBUG_CMD_START_LABEL(cb, "Final Copy");
            cb.begin(vk::CommandBufferBeginInfo{});
            // Prepare barriers
            for (size_t i = 0; i < operations.size(); i++) {
                barriers[i] = vk::ImageMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eNone,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferRead,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::QueueFamilyIgnored,
                    vk::QueueFamilyIgnored,
                    operations[i].image,
                    vk::ImageSubresourceRange{
                        vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
                    }
                };
            }
            *(barriers.rbegin()) = vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eNone, // > Set up execution dep instead of memory dep.
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                dst,
                vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
                }
            };
            vk::DependencyInfo dep{
                {}, {}, {}, barriers
            };
            cb.pipelineBarrier2(dep);

            // Record copying command.
            for (const auto & op : operations) {
                cb.copyImage(
                    op.image,
                    vk::ImageLayout::eTransferSrcOptimal,
                    dst,
                    vk::ImageLayout::eTransferDstOptimal,
                    {
                        vk::ImageCopy{
                            vk::ImageSubresourceLayers{
                                vk::ImageAspectFlagBits::eColor,
                                0, 0, 1
                            },
                            vk::Offset3D{op.offset_src, 0},
                            vk::ImageSubresourceLayers{
                                vk::ImageAspectFlagBits::eColor,
                                0, 0, 1
                            },
                            vk::Offset3D{op.offset_dst, 0},
                            vk::Extent3D{op.extent, 1}
                        }
                    }
                );
            }

            // Prepare another set of barriers
            for (size_t i = 0; i < operations.size(); i++) {
                barriers[i] = vk::ImageMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferRead,
                    vk::PipelineStageFlagBits2::eBottomOfPipe,
                    vk::AccessFlagBits2::eNone,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::QueueFamilyIgnored,
                    vk::QueueFamilyIgnored,
                    operations[i].image,
                    vk::ImageSubresourceRange{
                        vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
                    }
                };
            }
            *(barriers.rbegin()) = vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eBottomOfPipe,
                vk::AccessFlagBits2::eNone,
                vk::ImageLayout::eTransferDstOptimal,
                is_framebuffer ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eReadOnlyOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                dst,
                vk::ImageSubresourceRange{
                    vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
                }
            };
            cb.pipelineBarrier2(dep);
            cb.end();
            DEBUG_CMD_END_LABEL(cb);
        };
    };

    FrameManager::FrameManager(RenderSystem &sys) : m_system(sys)
    {
    }

    FrameManager::~FrameManager() = default;

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
        m_presenting_helper = std::make_unique <PresentingHelper> ();
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

    void FrameManager::StageCopyComposition(vk::Image image, vk::Extent2D extent, vk::Offset2D offsetSrc, vk::Offset2D offsetDst)
    {
        m_presenting_helper->operations.emplace_back(image, extent, offsetSrc, offsetDst);
    }

    void FrameManager::StageCopyComposition(vk::Image image)
    {
        StageCopyComposition(image, m_system.GetSwapchain().GetExtent());
    }

    bool FrameManager::CompositeToFramebufferAndPresent()
    {
        // Copy framebuffers
        const auto fif = GetFrameInFlight();
        const auto framebuffer_image = this->m_system.GetSwapchain().GetImages()[GetFramebuffer()];
        const auto & copy_cb = copy_to_swapchain_command_buffers[fif].get();

        m_presenting_helper->RecordCopyCommand(copy_cb, framebuffer_image);
        m_presenting_helper->operations.clear();

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
            {copy_cb},
            ss
        };
        graphic_queue.submit(sinfo, this->command_executed_fences[this->GetFrameInFlight()].get());

        // Queue a present directive
        std::array<vk::SwapchainKHR, 1> swapchains { swapchain };
        std::array<uint32_t, 1> frame_indices { GetFramebuffer() };
        // Wait for command buffer before presenting the frame
        std::array<vk::Semaphore, 1> semaphores {copy_to_swapchain_completed_semaphores[fif].get()};

        bool needs_recreating = false;
        try {
            vk::PresentInfoKHR info{semaphores, swapchains, frame_indices};
            vk::Result result = present_queue.presentKHR(info);
            if (result != vk::Result::eSuccess) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER, 
                    "Presenting returned %s other than success.",
                    vk::to_string(result).c_str()
                    );
            }
        } catch (vk::OutOfDateKHRError & e) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Swapchain out of date.");
            needs_recreating = true;
        }
        
        CompleteFrame();
        return needs_recreating;
    }

    vk::Fence FrameManager::CompositeToImage(vk::Image image, uint64_t timeout)
    {
        // Copy framebuffers
        const auto fif = GetFrameInFlight();
        const auto & copy_cb = copy_to_swapchain_command_buffers[fif].get();

        m_presenting_helper->RecordCopyCommand(copy_cb, image, false);
        m_presenting_helper->operations.clear();

        // Wait for command execution.
        std::array <vk::Semaphore, 2> rces = {
            render_command_executed_semaphores[fif].get(),
            image_acquired_semaphores[fif].get()        // > Although the image is not needed, the semaphore must be cleared.
        };
        std::array <vk::PipelineStageFlags, 2> psfb = {
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer
        };

        // Signal ready for next frame.
        std::array <vk::Semaphore, 1> ss = {
            next_frame_ready_semaphores[fif].get(),
        };

        vk::SubmitInfo sinfo {
            rces,
            psfb,
            {copy_cb},
            ss
        };
        graphic_queue.submit(sinfo, this->command_executed_fences[this->GetFrameInFlight()].get());

        if (timeout) {
            auto device = m_system.getDevice();
            auto result = device.waitForFences({this->command_executed_fences[fif].get()}, true, timeout);
            if (result != vk::Result::eSuccess) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Timed out waiting for composition for frame id %u.", fif);
            }
            device.resetFences({this->command_executed_fences[fif].get()});
        }

        CompleteFrame();
        return this->command_executed_fences[fif].get();
    }

    void FrameManager::CompleteFrame()
    {
        // Increment FIF counter, reset framebuffer index
        current_frame_in_flight = (current_frame_in_flight + 1) % FRAMES_IN_FLIGHT;
        current_framebuffer = std::numeric_limits<uint32_t>::max();

        // Handle submissions
        m_submission_helper->CompleteFrame();
    }
    void FrameManager::UpdateSwapchain()
    {
        this->swapchain = m_system.GetSwapchain().GetSwapchain();
    }
    SubmissionHelper &FrameManager::GetSubmissionHelper()
    {
        return *m_submission_helper;
    }
}
