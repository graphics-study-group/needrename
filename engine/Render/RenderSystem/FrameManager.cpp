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

#include "Render/RenderSystem/FrameSemaphore.hpp"

#include <SDL3/SDL.h>

namespace {
    void RecordCopyCommand(
        const vk::CommandBuffer &cb,
        const vk::Image &src,
        vk::Extent2D extent_src,
        vk::Offset2D offset_src,
        vk::Extent2D extent_dst,
        vk::Offset2D offset_dst,
        const vk::Image &dst,
        vk::Filter filter
    ) {
        std::array<vk::ImageMemoryBarrier2, 2> barriers {};

        cb.begin(vk::CommandBufferBeginInfo{});
        DEBUG_CMD_START_LABEL(cb, "Final Copy");
        barriers[0] = vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryWrite,
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferRead,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            src,
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
        barriers[1] = vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eNone, // > Set up execution dep instead of memory dep.
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            dst,
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, barriers});

        cb.blitImage(
            src,
            vk::ImageLayout::eTransferSrcOptimal,
            dst,
            vk::ImageLayout::eTransferDstOptimal,
            {vk::ImageBlit{
                vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {vk::Offset3D{offset_src, 0},
                    vk::Offset3D{
                        offset_src.x + extent_src.width,
                        offset_src.y + extent_src.height,
                        1
                    }},
                vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {vk::Offset3D{offset_dst, 0},
                    vk::Offset3D{
                        offset_dst.x + extent_dst.width,
                        offset_dst.y + extent_dst.height,
                        1
                    }}
            }},
            filter
        );

        barriers[0] = vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferRead,
            vk::PipelineStageFlagBits2::eNone,
            vk::AccessFlagBits2::eNone,
            vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            src,
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
        barriers[1] = vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eHost,
            vk::AccessFlagBits2::eMemoryRead,
            vk::ImageLayout::eTransferDstOptimal,
             vk::ImageLayout::ePresentSrcKHR,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            dst,
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, barriers});

        cb.end();
    }
}

namespace Engine::RenderSystemState {
    struct FrameManager::impl {
        
        std::array<FrameSemaphore, FRAMES_IN_FLIGHT> timeline_semaphores{};

        std::array<vk::UniqueSemaphore, FRAMES_IN_FLIGHT> image_acquired_semaphores{};

        // This has to be a vector since swapchain image count are not determined until startup.
        std::vector<vk::UniqueSemaphore> copy_to_swapchain_completed_semaphores{};

        std::array<vk::UniqueFence, FRAMES_IN_FLIGHT> command_executed_fences{};

        std::array<vk::UniqueCommandBuffer, FRAMES_IN_FLIGHT> command_buffers{};
        std::array<vk::UniqueCommandBuffer, FRAMES_IN_FLIGHT> copy_to_swapchain_command_buffers{};

        uint32_t current_frame_in_flight{std::numeric_limits<uint32_t>::max()};

        uint32_t current_framebuffer{std::numeric_limits<uint32_t>::max()};

        uint64_t total_frame_count{0};

        RenderSystem &m_system;

        std::unique_ptr<SubmissionHelper> m_submission_helper{};

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

        vk::SemaphoreCreateInfo scinfo{};
        vk::SemaphoreTypeCreateInfo stcinfo{};
        stcinfo.semaphoreType = vk::SemaphoreType::eBinary;
        stcinfo.initialValue = 0;
        scinfo.pNext = &stcinfo;
    
        vk::FenceCreateInfo finfo{{vk::FenceCreateFlagBits::eSignaled}};
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            image_acquired_semaphores[i] = device.createSemaphoreUnique(scinfo);
            DEBUG_SET_NAME_TEMPLATE(
                device, image_acquired_semaphores[i].get(), std::format("Semaphore - image acquired {}", i)
            );
    
            command_executed_fences[i] = device.createFenceUnique(finfo);
            DEBUG_SET_NAME_TEMPLATE(
                device, command_executed_fences[i].get(), std::format("Fence - all commands executed {}", i)
            );
        }
        copy_to_swapchain_completed_semaphores.resize(m_system.GetSwapchain().GetFrameCount());
        for (size_t i = 0; i < copy_to_swapchain_completed_semaphores.size(); i++) {
            copy_to_swapchain_completed_semaphores[i] = device.createSemaphoreUnique(scinfo);
            DEBUG_SET_NAME_TEMPLATE(
                device,
                copy_to_swapchain_completed_semaphores[i].get(),
                std::format("Semaphore - final copy completed {}", i)
            );
        }

        stcinfo.semaphoreType = vk::SemaphoreType::eTimeline;
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            timeline_semaphores[i].timeline_semaphore = device.createSemaphoreUnique(scinfo);
            DEBUG_SET_NAME_TEMPLATE(
                device,
                timeline_semaphores[i].timeline_semaphore.get(),
                std::format("Semaphore - timeline semaphore {}", i)
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

        // Signal start of this frame
        device.signalSemaphore(
            vk::SemaphoreSignalInfo{
                pimpl->timeline_semaphores[fif].timeline_semaphore.get(),
                pimpl->timeline_semaphores[fif].GetTimepointValue(FrameSemaphore::TimePoint::Pending)
            }
        );

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
        vk::SubmitInfo2 info{};
        vk::CommandBufferSubmitInfo cbsi{pimpl->command_buffers[fif].get()};
        info.setCommandBufferInfos({cbsi});

        vk::SemaphoreSubmitInfo wait_info{}, signal_info{};
        wait_info = pimpl->timeline_semaphores[fif].GetSubmitInfo(
            // Currently we wait for transfer finish as there is no pre-compute stage.
            FrameSemaphore::TimePoint::PreTransferFinished, 
            // Wait before any command starts.
            vk::PipelineStageFlagBits2::eAllCommands
        );
        signal_info = pimpl->timeline_semaphores[fif].GetSubmitInfo(
            // Currently we signal directly for the post-compute as there is no post-compute stage.
            FrameSemaphore::TimePoint::PostComputeFinished,
            // Signal after all commands are finished.
            vk::PipelineStageFlagBits2::eAllCommands
        );

        info.setWaitSemaphoreInfos({wait_info});
        info.setSignalSemaphoreInfos({signal_info});

        this->pimpl->m_system.GetDeviceInterface().GetQueueInfo().graphicsQueue.submit2({info}, nullptr);

        pimpl->m_submission_helper->OnPostMainCbSubmission();
    }

    bool FrameManager::PresentToFramebuffer(
        vk::Image image, vk::Extent2D extentSrc, vk::Offset2D offsetSrc, vk::Filter filter
    ) {
        const auto fif = GetFrameInFlight();
        const auto framebuffer_image = this->pimpl->m_system.GetSwapchain().GetImages()[GetFramebuffer()];
        const auto &copy_cb = pimpl->copy_to_swapchain_command_buffers[fif].get();

        RecordCopyCommand(
            copy_cb,
            image, 
            extentSrc,
            offsetSrc, 
            this->pimpl->m_system.GetSwapchain().GetExtent(),
            {0, 0},
            framebuffer_image,
            vk::Filter::eLinear
        );

        // Prepare submit info for copy commandbuffer
        vk::CommandBufferSubmitInfo cbsi{copy_cb};
        std::array <vk::SemaphoreSubmitInfo, 2> wait_infos {};
        std::array <vk::SemaphoreSubmitInfo, 1> signal_infos {};

        // Wait for post compute
        wait_infos[0] = pimpl->timeline_semaphores[fif].GetSubmitInfo(
            FrameSemaphore::TimePoint::PostComputeFinished,
            vk::PipelineStageFlagBits2::eAllTransfer
        );
        // Wait for image acquisition (this is binary).
        wait_infos[1] = vk::SemaphoreSubmitInfo{
            pimpl->image_acquired_semaphores[fif].get(), 
            0,
            vk::PipelineStageFlagBits2::eAllTransfer
        };

        // Signal ready for presenting.
        signal_infos[0] = vk::SemaphoreSubmitInfo{
            pimpl->copy_to_swapchain_completed_semaphores[GetFramebuffer()].get(),
            0,
            vk::PipelineStageFlagBits2::eHost
        };

        vk::SubmitInfo2 sinfo{vk::SubmitFlags{}, wait_infos, {cbsi}, signal_infos};
        const auto &queueInfo = pimpl->m_system.GetDeviceInterface().GetQueueInfo();
        queueInfo.presentQueue.submit2(sinfo, this->pimpl->command_executed_fences[this->GetFrameInFlight()].get());

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

    void FrameManager::impl::CompleteFrame() {
        // Increment FIF counter, reset framebuffer index
        timeline_semaphores[current_frame_in_flight].StepFrame();
        current_frame_in_flight = (current_frame_in_flight + 1) % FRAMES_IN_FLIGHT;
        current_framebuffer = std::numeric_limits<uint32_t>::max();
        total_frame_count++;

        // Handle submissions
        m_submission_helper->OnFrameComplete();
    }

    SubmissionHelper &FrameManager::GetSubmissionHelper() {
        return *(pimpl->m_submission_helper);
    }
    const FrameSemaphore & FrameManager::GetFrameSemaphore() const noexcept {
        return pimpl->timeline_semaphores[GetFrameInFlight()];
    }
} // namespace Engine::RenderSystemState
