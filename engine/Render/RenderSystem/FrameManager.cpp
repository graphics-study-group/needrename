#include "FrameManager.h"

#include "Render/DebugUtils.h"
#include "Render/Memory/Buffer.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/CommandBuffer/ComputeContext.h"
#include "Render/Pipeline/CommandBuffer/GraphicsContext.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/Structs.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/RenderSystem/DeviceInterface.h"

#include <SDL3/SDL.h>

namespace Engine::RenderSystemState {
    struct PresentingHelper {
        struct PresentingOperation {
            vk::Image image;
            uint32_t src_queue_family, present_queue_family;

            bool is_blitting;
            union Parameters {
                struct {
                    vk::Extent2D extent;
                    vk::Offset2D offset_src;
                    vk::Offset2D offset_dst;
                } copy;
                struct {
                    vk::Extent2D extent_src;
                    vk::Extent2D extent_dst;
                    vk::Offset2D offset_src;
                    vk::Offset2D offset_dst;
                    vk::Filter filter;
                } blit;
            } parameters;
        };

        std::vector<PresentingOperation> operations{};

        /**
         * @brief Record the command buffer according to the saved operations.
         * The operations
         * saved in the struct are left untouched.
         *
         * `vkBeginCommandBuffer` and `vkEndCommandBuffer`
         * are called on the buffer in this method, and therefore
         * the buffer is expected to be in Pending
         * state when called, and will be in Executable state after calling.
         *
         * The method transits
         * the image to Transfer Source Layout and the dst to Transfer Destination Layout,
         * record a image
         * copy command (not blitting command, so resizing is not possible), and transits the image
         * back to
         * Color Attachment Optimal layout.
         */
        void RecordCopyCommand(const vk::CommandBuffer &cb, const vk::Image &dst, bool is_framebuffer = true) const {
            // We can cache this vector to further speed up recording.
            std::vector<vk::ImageMemoryBarrier2> barriers(operations.size() + 1, vk::ImageMemoryBarrier2{});

            cb.begin(vk::CommandBufferBeginInfo{});
            DEBUG_CMD_START_LABEL(cb, "Final Copy");

            // Prepare barriers
            for (size_t i = 0; i < operations.size(); i++) {
                bool ignore_queue_families = (operations[i].present_queue_family == operations[i].src_queue_family);
                barriers[i] = vk::ImageMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eNone,
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferRead,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageLayout::eTransferSrcOptimal,
                    ignore_queue_families ? vk::QueueFamilyIgnored : operations[i].src_queue_family,
                    ignore_queue_families ? vk::QueueFamilyIgnored : operations[i].present_queue_family,
                    operations[i].image,
                    vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
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
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
            };
            vk::DependencyInfo dep{{}, {}, {}, barriers};
            cb.pipelineBarrier2(dep);

            // Record copying command.
            for (const auto &op : operations) {
                if (op.is_blitting) {
                    cb.blitImage(
                        op.image,
                        vk::ImageLayout::eTransferSrcOptimal,
                        dst,
                        vk::ImageLayout::eTransferDstOptimal,
                        {vk::ImageBlit{
                            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                            {vk::Offset3D{op.parameters.blit.offset_src, 0},
                             vk::Offset3D{
                                 op.parameters.blit.offset_src.x + op.parameters.blit.extent_src.width,
                                 op.parameters.blit.offset_src.y + op.parameters.blit.extent_src.height,
                                 1
                             }},
                            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                            {vk::Offset3D{op.parameters.blit.offset_dst, 0},
                             vk::Offset3D{
                                 op.parameters.blit.offset_dst.x + op.parameters.blit.extent_dst.width,
                                 op.parameters.blit.offset_dst.y + op.parameters.blit.extent_dst.height,
                                 1
                             }}
                        }},
                        op.parameters.blit.filter
                    );
                } else {
                    cb.copyImage(
                        op.image,
                        vk::ImageLayout::eTransferSrcOptimal,
                        dst,
                        vk::ImageLayout::eTransferDstOptimal,
                        {vk::ImageCopy{
                            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                            vk::Offset3D{op.parameters.copy.offset_src, 0},
                            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                            vk::Offset3D{op.parameters.copy.offset_dst, 0},
                            vk::Extent3D{op.parameters.copy.extent, 1}
                        }}
                    );
                }
            }

            // Prepare another set of barriers
            for (size_t i = 0; i < operations.size(); i++) {
                bool ignore_queue_families = (operations[i].present_queue_family == operations[i].src_queue_family);
                barriers[i] = vk::ImageMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferRead,
                    vk::PipelineStageFlagBits2::eBottomOfPipe,
                    vk::AccessFlagBits2::eNone,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    ignore_queue_families ? vk::QueueFamilyIgnored : operations[i].present_queue_family,
                    ignore_queue_families ? vk::QueueFamilyIgnored : operations[i].src_queue_family,
                    operations[i].image,
                    vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
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
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
            };
            cb.pipelineBarrier2(dep);

            DEBUG_CMD_END_LABEL(cb);
            cb.end();
        };
    };

    struct FrameManager::impl {
        std::array<vk::UniqueSemaphore, FRAMES_IN_FLIGHT> image_acquired_semaphores{};
        std::array<vk::UniqueSemaphore, FRAMES_IN_FLIGHT> render_command_executed_semaphores{};
        // std::array <vk::UniqueSemaphore, FRAMES_IN_FLIGHT> copy_to_swapchain_completed_semaphores {};
        std::vector<vk::UniqueSemaphore> copy_to_swapchain_completed_semaphores{};
        std::array<vk::UniqueSemaphore, FRAMES_IN_FLIGHT> next_frame_ready_semaphores{};
        std::array<vk::UniqueFence, FRAMES_IN_FLIGHT> command_executed_fences{};
        std::array<vk::UniqueCommandBuffer, FRAMES_IN_FLIGHT> command_buffers{};
        std::array<vk::UniqueCommandBuffer, FRAMES_IN_FLIGHT> copy_to_swapchain_command_buffers{};

        uint32_t current_frame_in_flight{std::numeric_limits<uint32_t>::max()};

        // Current frame buffer id. Set by `StartFrame()` method.
        uint32_t current_framebuffer{std::numeric_limits<uint32_t>::max()};

        uint64_t total_frame_count{0};

        /* vk::Queue graphic_queue {};
        vk::Queue present_queue {};
        vk::SwapchainKHR swapchain {}; */
        RenderSystem &m_system;

        std::unique_ptr<SubmissionHelper> m_submission_helper{};
        std::unique_ptr<PresentingHelper> m_presenting_helper{};

        /// @brief Progress the frame state machine.
        void CompleteFrame();

        impl(RenderSystem &sys) : m_system(sys) {};
        void Create();
    };

    FrameManager::FrameManager(RenderSystem &sys) : pimpl(std::make_unique<impl>(sys)) {
    }

    FrameManager::~FrameManager() = default;

    void FrameManager::impl::Create() {
        auto device = m_system.GetDevice();

        vk::SemaphoreCreateInfo sinfo{};
        vk::FenceCreateInfo finfo{{vk::FenceCreateFlagBits::eSignaled}};
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            image_acquired_semaphores[i] = device.createSemaphoreUnique(sinfo);
            DEBUG_SET_NAME_TEMPLATE(
                device, image_acquired_semaphores[i].get(), std::format("Semaphore - image acquired {}", i)
            );

            render_command_executed_semaphores[i] = device.createSemaphoreUnique(sinfo);
            DEBUG_SET_NAME_TEMPLATE(
                device, render_command_executed_semaphores[i].get(), std::format("Semaphore - render CB executed {}", i)
            );

            next_frame_ready_semaphores[i] = device.createSemaphoreUnique(sinfo);
            DEBUG_SET_NAME_TEMPLATE(
                device, next_frame_ready_semaphores[i].get(), std::format("Semaphore - next frame ready {}", i)
            );

            command_executed_fences[i] = device.createFenceUnique(finfo);
            DEBUG_SET_NAME_TEMPLATE(
                device, command_executed_fences[i].get(), std::format("Fence - all commands executed {}", i)
            );
        }

        copy_to_swapchain_completed_semaphores.resize(m_system.GetSwapchain().GetFrameCount());
        for (size_t i = 0; i < copy_to_swapchain_completed_semaphores.size(); i++) {
            copy_to_swapchain_completed_semaphores[i] = device.createSemaphoreUnique(sinfo);
            DEBUG_SET_NAME_TEMPLATE(
                device,
                copy_to_swapchain_completed_semaphores[i].get(),
                std::format("Semaphore - final copy completed {}", i)
            );
        }

        // Allocate main render command buffers
        const auto & queue_info = m_system.GetDeviceInterface().GetQueueInfo();
        auto new_command_buffers = device.allocateCommandBuffersUnique(
            vk::CommandBufferAllocateInfo{
                queue_info.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, FRAMES_IN_FLIGHT
            }
        );
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            command_buffers[i] = std::move(new_command_buffers[i]);
            DEBUG_SET_NAME_TEMPLATE(
                device, command_buffers[i].get(), std::format("Command buffer - main render {}", i)
            );
        }

        // Allocate copying and presenting command buffers
        new_command_buffers = device.allocateCommandBuffersUnique(
            vk::CommandBufferAllocateInfo{
                queue_info.presentPool.get(), vk::CommandBufferLevel::ePrimary, FRAMES_IN_FLIGHT
            }
        );
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            copy_to_swapchain_command_buffers[i] = std::move(new_command_buffers[i]);
            DEBUG_SET_NAME_TEMPLATE(
                device, copy_to_swapchain_command_buffers[i].get(), std::format("Command buffer - composition {}", i)
            );
        }

        current_frame_in_flight = 0;
        m_submission_helper = std::make_unique<SubmissionHelper>(m_system);
        m_presenting_helper = std::make_unique<PresentingHelper>();
    }

    void FrameManager::Create() {
        pimpl->Create();
    }

    uint32_t FrameManager::GetFrameInFlight() const noexcept {
        assert(this->pimpl->current_frame_in_flight < FRAMES_IN_FLIGHT && "Frame Manager is in invalid state.");
        return this->pimpl->current_frame_in_flight;
    }

    uint64_t FrameManager::GetTotalFrame() const noexcept {
        return pimpl->total_frame_count;
    }

    uint32_t FrameManager::GetFramebuffer() const noexcept {
        assert(
            this->pimpl->current_framebuffer < std::numeric_limits<uint32_t>::max()
            && "Frame Manager is in invalid state."
        );
        return this->pimpl->current_framebuffer;
    }

    GraphicsCommandBuffer FrameManager::GetCommandBuffer() {
        assert(
            this->pimpl->current_framebuffer < std::numeric_limits<uint32_t>::max()
            && "Frame Manager is in invalid state."
        );
        return GraphicsCommandBuffer(pimpl->m_system, GetRawMainCommandBuffer(), GetFrameInFlight());
    }

    GraphicsContext FrameManager::GetGraphicsContext() {
        return GraphicsContext(pimpl->m_system, GetRawMainCommandBuffer(), GetFrameInFlight());
    }

    ComputeContext FrameManager::GetComputeContext() {
        return ComputeContext(pimpl->m_system, GetRawMainCommandBuffer(), GetFrameInFlight());
    }

    vk::CommandBuffer FrameManager::GetRawMainCommandBuffer() {
        return pimpl->command_buffers[GetFrameInFlight()].get();
    }

    uint32_t FrameManager::StartFrame(uint64_t timeout) {

        auto device = pimpl->m_system.GetDevice();
        uint32_t fif = GetFrameInFlight();

        // Wait for command buffer execution.
        vk::Fence fence = pimpl->command_executed_fences[fif].get();
        vk::Result wait_result = device.waitForFences({fence}, vk::True, timeout);
        if (wait_result == vk::Result::eTimeout) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Timed out waiting for fence for frame id %u.", fif);
        }
        pimpl->command_buffers[fif]->reset();
        device.resetFences({fence});

        // Acquire new image
        auto acquire_result = device.acquireNextImageKHR(
            pimpl->m_system.GetSwapchain().GetSwapchain(), timeout, pimpl->image_acquired_semaphores[fif].get(), nullptr
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
        pimpl->current_framebuffer = acquire_result.value;

        pimpl->m_submission_helper->OnFrameStart();

        return pimpl->current_framebuffer;
    }

    void FrameManager::SubmitMainCommandBuffer() {
        pimpl->m_submission_helper->OnPreMainCbSubmission();

        bool wait_for_semaphore = (pimpl->total_frame_count > 0);
        uint32_t fif = GetFrameInFlight();
        vk::SubmitInfo info{};
        vk::CommandBuffer cb = pimpl->command_buffers[fif].get();
        info.commandBufferCount = 1;
        info.pCommandBuffers = &cb;
        if (wait_for_semaphore) {
            // Stall the execution of this commandbuffer until previous one has finished.
            auto waitFlags = vk::PipelineStageFlags{vk::PipelineStageFlagBits::eTopOfPipe};
            info.setWaitSemaphores(
                {this->pimpl->next_frame_ready_semaphores[(fif + FRAMES_IN_FLIGHT - 1) % FRAMES_IN_FLIGHT].get()}
            );
            info.pWaitDstStageMask = &waitFlags;
        } else {
            info.waitSemaphoreCount = 0;
        }

        info.setSignalSemaphores({this->pimpl->render_command_executed_semaphores[fif].get()});
        std::array<vk::SubmitInfo, 1> infos{info};
        this->pimpl->m_system.GetDeviceInterface().GetQueueInfo().graphicsQueue.submit(infos, nullptr);

        pimpl->m_submission_helper->OnPostMainCbSubmission();
    }

    void FrameManager::StageCopyComposition(
        vk::Image image, vk::Extent2D extent, vk::Offset2D offsetSrc, vk::Offset2D offsetDst
    ) {
        const auto &families = pimpl->m_system.GetDeviceInterface().GetQueueFamilies();
        PresentingHelper::PresentingOperation copy_op{
            .image = image,
            .src_queue_family = families.graphics.value(),
            .present_queue_family = families.present.value(),
            .is_blitting = false,
            .parameters = {.copy = {.extent = extent, .offset_src = offsetSrc, .offset_dst = offsetDst}}
        };
        pimpl->m_presenting_helper->operations.push_back(std::move(copy_op));
    }

    void FrameManager::StageBlitComposition(
        vk::Image image,
        vk::Extent2D extentSrc,
        vk::Extent2D extentDst,
        vk::Offset2D offsetSrc,
        vk::Offset2D offsetDst,
        vk::Filter filter
    ) {
        const auto &families = pimpl->m_system.GetDeviceInterface().GetQueueFamilies();
        PresentingHelper::PresentingOperation blit_op{
            .image = image,
            .src_queue_family = families.graphics.value(),
            .present_queue_family = families.present.value(),
            .is_blitting = true,
            .parameters = {
                .blit = {
                    .extent_src = extentSrc,
                    .extent_dst = extentDst,
                    .offset_src = offsetSrc,
                    .offset_dst = offsetDst,
                    .filter = filter
                }
            }
        };
        pimpl->m_presenting_helper->operations.push_back(std::move(blit_op));
    }

    void FrameManager::StageCopyComposition(vk::Image image) {
        StageCopyComposition(image, pimpl->m_system.GetSwapchain().GetExtent());
    }

    bool FrameManager::CompositeToFramebufferAndPresent() {
        // Copy framebuffers
        const auto fif = GetFrameInFlight();
        const auto framebuffer_image = this->pimpl->m_system.GetSwapchain().GetImages()[GetFramebuffer()];
        const auto &copy_cb = pimpl->copy_to_swapchain_command_buffers[fif].get();

        pimpl->m_presenting_helper->RecordCopyCommand(copy_cb, framebuffer_image);
        pimpl->m_presenting_helper->operations.clear();

        // Wait for both command execution and image aquisition.
        std::array<vk::Semaphore, 2> rces = {
            pimpl->render_command_executed_semaphores[fif].get(), pimpl->image_acquired_semaphores[fif].get()
        };
        std::array<vk::PipelineStageFlags, 2> psfb = {
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer
        };

        // Signal ready for presenting and for next frame.
        std::array<vk::Semaphore, 2> ss = {
            pimpl->copy_to_swapchain_completed_semaphores[GetFramebuffer()].get(),
            pimpl->next_frame_ready_semaphores[fif].get()
        };

        vk::SubmitInfo sinfo{rces, psfb, {copy_cb}, ss};
        const auto &queueInfo = pimpl->m_system.GetDeviceInterface().GetQueueInfo();
        queueInfo.presentQueue.submit(sinfo, this->pimpl->command_executed_fences[this->GetFrameInFlight()].get());

        // Queue a present directive
        std::array<vk::SwapchainKHR, 1> swapchains{pimpl->m_system.GetSwapchain().GetSwapchain()};
        std::array<uint32_t, 1> frame_indices{GetFramebuffer()};
        // Wait for command buffer before presenting the frame
        std::array<vk::Semaphore, 1> semaphores{pimpl->copy_to_swapchain_completed_semaphores[GetFramebuffer()].get()};

        bool needs_recreating = false;
        try {
            vk::PresentInfoKHR info{semaphores, swapchains, frame_indices};
            vk::Result result = queueInfo.presentQueue.presentKHR(info);
            if (result != vk::Result::eSuccess) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER, "Presenting returned %s other than success.", vk::to_string(result).c_str()
                );
            }
        } catch (vk::OutOfDateKHRError &e) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Swapchain out of date.");
            needs_recreating = true;
        }

        pimpl->CompleteFrame();
        return needs_recreating;
    }

    vk::Fence FrameManager::CompositeToImage(vk::Image image, uint64_t timeout) {
        // Copy framebuffers
        const auto fif = GetFrameInFlight();
        const auto &copy_cb = pimpl->copy_to_swapchain_command_buffers[fif].get();

        pimpl->m_presenting_helper->RecordCopyCommand(copy_cb, image, false);
        pimpl->m_presenting_helper->operations.clear();

        // Wait for command execution.
        std::array<vk::Semaphore, 2> rces = {
            pimpl->render_command_executed_semaphores[fif].get(),
            pimpl->image_acquired_semaphores[fif]
                .get() // > Although the image is not needed, the semaphore must be cleared.
        };
        std::array<vk::PipelineStageFlags, 2> psfb = {
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer
        };

        // Signal ready for next frame.
        std::array<vk::Semaphore, 1> ss = {
            pimpl->next_frame_ready_semaphores[fif].get(),
        };

        vk::SubmitInfo sinfo{rces, psfb, {copy_cb}, ss};
        pimpl->m_system.GetDeviceInterface().GetQueueInfo().presentQueue.submit(
            sinfo, this->pimpl->command_executed_fences[this->GetFrameInFlight()].get()
        );

        if (timeout) {
            auto device = pimpl->m_system.GetDevice();
            auto result = device.waitForFences({this->pimpl->command_executed_fences[fif].get()}, true, timeout);
            if (result != vk::Result::eSuccess) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Timed out waiting for composition for frame id %u.", fif);
            }
            device.resetFences({this->pimpl->command_executed_fences[fif].get()});
        }

        pimpl->CompleteFrame();
        return this->pimpl->command_executed_fences[fif].get();
    }

    void FrameManager::impl::CompleteFrame() {
        // Increment FIF counter, reset framebuffer index
        current_frame_in_flight = (current_frame_in_flight + 1) % FRAMES_IN_FLIGHT;
        current_framebuffer = std::numeric_limits<uint32_t>::max();
        total_frame_count++;

        // Handle submissions
        m_submission_helper->OnFrameComplete();
    }

    SubmissionHelper &FrameManager::GetSubmissionHelper() {
        return *(pimpl->m_submission_helper);
    }
} // namespace Engine::RenderSystemState
