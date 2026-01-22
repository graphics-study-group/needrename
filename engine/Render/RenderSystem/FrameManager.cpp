#include "FrameManager.h"

#include "Render/DebugUtils.h"
#include "Render/Memory/DeviceBuffer.h"
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
#include <bitset>

namespace {
    void RecordCopyCommand(
        const vk::CommandBuffer &cb,
        const vk::Image &src,
        vk::Extent2D extent_src,
        vk::Offset2D offset_src,
        vk::Extent2D extent_dst,
        vk::Offset2D offset_dst,
        const Engine::RenderSystemState::Swapchain & swapchain,
        uint32_t framebuffer,
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
        barriers[1] = swapchain.GetPreCopyBarrier(framebuffer);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, barriers});

        cb.blitImage(
            src,
            vk::ImageLayout::eTransferSrcOptimal,
            swapchain.GetImages()[framebuffer],
            vk::ImageLayout::eTransferDstOptimal,
            {vk::ImageBlit{
                vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {vk::Offset3D{offset_src, 0},
                    vk::Offset3D{
                        offset_src.x + (int32_t)extent_src.width,
                        offset_src.y + (int32_t)extent_src.height,
                        1
                    }},
                vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {vk::Offset3D{offset_dst, 0},
                    vk::Offset3D{
                        offset_dst.x + (int32_t)extent_dst.width,
                        offset_dst.y + (int32_t)extent_dst.height,
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
        barriers[1] = swapchain.GetPostCopyBarrier(framebuffer);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, barriers});
        DEBUG_CMD_END_LABEL(cb);
        cb.end();
    }

    void ReadbackCommand(vk::CommandBuffer cb, const Engine::DeviceBuffer & src, const Engine::DeviceBuffer & dst) {
        using namespace Engine;
        cb.copyBuffer(
            src.GetBuffer(), dst.GetBuffer(),
            vk::BufferCopy{0, 0, vk::WholeSize}
        );
    }

    void ReadbackCommand(
        vk::CommandBuffer cb,
        const Engine::Texture & src,
        vk::ImageAspectFlagBits aspect,
        uint32_t level,
        uint32_t layer,
        vk::Extent3D extent,
        const Engine::DeviceBuffer & dst
    ) {
        using namespace Engine;

        cb.copyImageToBuffer(
            src.GetImage(),
            vk::ImageLayout::eTransferSrcOptimal,
            dst.GetBuffer(),
            vk::BufferImageCopy{
                0, 0, 0,
                vk::ImageSubresourceLayers{
                    aspect,
                    level, layer, 1
                },
                vk::Offset3D{0, 0, 0},
                extent
            }
        );
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

        // Data and handles used by readback routines.
        struct {
            std::bitset <FRAMES_IN_FLIGHT> has_post_graphics_rb{};
            std::array <vk::UniqueFence, FRAMES_IN_FLIGHT> post_graphics_rb_fences {};
            std::array <vk::UniqueCommandBuffer, FRAMES_IN_FLIGHT> post_graphics_rb_cbs {};
            std::queue <std::function<void(vk::CommandBuffer)>> post_graphics_commands {};
        } readback {};

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

            readback.post_graphics_rb_fences[i] = device.createFenceUnique(finfo);
            DEBUG_SET_NAME_TEMPLATE(
                device, readback.post_graphics_rb_fences[i].get(), std::format("Fence - post graphics readback executed {}", i)
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

        // Readback command buffers
        new_command_buffers = device.allocateCommandBuffersUnique(
            vk::CommandBufferAllocateInfo{
                queue_info.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, FRAMES_IN_FLIGHT
            }
        );
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            readback.post_graphics_rb_cbs[i] = std::move(new_command_buffers[i]);
            DEBUG_SET_NAME_TEMPLATE(
                device, readback.post_graphics_rb_cbs[i].get(), std::format("Command buffer - post graphics readback {}", i)
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
        // Prevent validation layer from complaining
        if (pimpl->timeline_semaphores[fif].frame_count > 0) {
            device.signalSemaphore(
                vk::SemaphoreSignalInfo{
                    pimpl->timeline_semaphores[fif].timeline_semaphore.get(),
                    pimpl->timeline_semaphores[fif].GetTimepointValue(FrameSemaphore::TimePoint::Pending)
                }
            );
        }

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

        return pimpl->current_framebuffer;
    }

    void FrameManager::SubmitMainCommandBuffer() {
        pimpl->m_submission_helper->OnPreMainCbSubmission();

        uint32_t fif = GetFrameInFlight();

        vk::CommandBufferSubmitInfo cbsi{pimpl->command_buffers[fif].get()};

        std::array<vk::SemaphoreSubmitInfo, 2> wait_infos{};
        vk::SemaphoreSubmitInfo signal_info{};

        wait_infos[0] = pimpl->timeline_semaphores[fif].GetSubmitInfo(
            // Currently we wait for transfer finish as there is no pre-compute stage.
            FrameSemaphore::TimePoint::PreTransferFinished, 
            // Wait before any command starts.
            vk::PipelineStageFlagBits2::eAllCommands
        );
        auto & prev_timeline_semaphore = pimpl->timeline_semaphores[(fif + (FRAMES_IN_FLIGHT - 1)) % FRAMES_IN_FLIGHT];
        wait_infos[1] = prev_timeline_semaphore.GetSubmitInfo(
            FrameSemaphore::TimePoint::CopyToPresentFinished,
            vk::PipelineStageFlagBits2::eAllCommands
        );
        // Ugly workaround for deadlock on the first frame.
        if (GetTotalFrame() == 0) {
            this->pimpl->m_system.GetDevice().signalSemaphore(
                vk::SemaphoreSignalInfo{
                    prev_timeline_semaphore.timeline_semaphore.get(),
                    prev_timeline_semaphore.GetTimepointValue(FrameSemaphore::TimePoint::CopyToPresentFinished)
                }
            );
        }
        // We must step frame after wait info is recorded to avoid deadlock.
        prev_timeline_semaphore.StepFrame();
    
        signal_info = pimpl->timeline_semaphores[fif].GetSubmitInfo(
            // Currently we signal directly for the post-compute as there is no post-compute stage.
            FrameSemaphore::TimePoint::PostComputeFinished,
            // Signal after all commands are finished.
            vk::PipelineStageFlagBits2::eAllCommands
        );

        this->pimpl->m_system.GetDeviceInterface().GetQueueInfo().graphicsQueue.submit2(
            vk::SubmitInfo2{
                vk::SubmitFlags{},
                wait_infos,
                {cbsi},
                {signal_info}
            }, 
            nullptr);
        
        // Record readback commands
        if (pimpl->readback.post_graphics_commands.empty()) return;

        assert(!pimpl->readback.has_post_graphics_rb[fif]);
        pimpl->readback.has_post_graphics_rb.set(fif);

        auto rbcb = pimpl->readback.post_graphics_rb_cbs[fif].get();
        rbcb.begin(vk::CommandBufferBeginInfo{
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        });
        while(!pimpl->readback.post_graphics_commands.empty()) {
            std::invoke(pimpl->readback.post_graphics_commands.front(), rbcb);
            pimpl->readback.post_graphics_commands.pop();
        }
        rbcb.end();

        wait_infos[0] = pimpl->timeline_semaphores[fif].GetSubmitInfo(
            FrameSemaphore::TimePoint::PostComputeFinished,
            vk::PipelineStageFlagBits2::eAllTransfer
        );
        cbsi.commandBuffer = rbcb;
        
        pimpl->m_system.GetDevice().resetFences({pimpl->readback.post_graphics_rb_fences[fif].get()});
        this->pimpl->m_system.GetDeviceInterface().GetQueueInfo().graphicsQueue.submit2(
            vk::SubmitInfo2{
                vk::SubmitFlags{},
                {wait_infos[0]},
                {cbsi},
                {}
            }, 
            pimpl->readback.post_graphics_rb_fences[fif].get());
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
            this->pimpl->m_system.GetSwapchain(),
            GetFramebuffer(),
            filter
        );

        // Prepare submit info for copy commandbuffer
        vk::CommandBufferSubmitInfo cbsi{copy_cb};
        std::array <vk::SemaphoreSubmitInfo, 2> wait_infos {};
        std::array <vk::SemaphoreSubmitInfo, 2> signal_infos {};

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
            vk::PipelineStageFlagBits2::eAllTransfer
        };
        signal_infos[1] = pimpl->timeline_semaphores[fif].GetSubmitInfo(
            FrameSemaphore::TimePoint::CopyToPresentFinished,
            vk::PipelineStageFlagBits2::eAllCommands
        );

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

        if (readback.has_post_graphics_rb[current_frame_in_flight]) {
            m_system.GetDevice().waitForFences(
                {readback.post_graphics_rb_fences[current_frame_in_flight].get()},
                true,
                std::numeric_limits<uint64_t>::max()
            );
            readback.has_post_graphics_rb.reset(current_frame_in_flight);
            readback.post_graphics_rb_cbs[current_frame_in_flight]->reset();
        }

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
    const FrameSemaphore & FrameManager::GetFrameSemaphore() const noexcept {
        return pimpl->timeline_semaphores[GetFrameInFlight()];
    }

    std::shared_ptr<DeviceBuffer> FrameManager::EnqueuePostGraphicsBufferReadback(const DeviceBuffer & device_buffer) {
        // This has to be a shared pointer as release time is undetermined.
        std::shared_ptr staging_buffer = DeviceBuffer::CreateUnique(
            pimpl->m_system.GetAllocatorState(),
            {BufferTypeBits::ReadbackFromDevice},
            device_buffer.GetSize()
        );

        auto enqueued = [&device_buffer, staging_buffer] (vk::CommandBuffer cb) -> void {
            ReadbackCommand(cb, device_buffer, *staging_buffer);
        };
        pimpl->readback.post_graphics_commands.push(enqueued);

        return staging_buffer;
    }
    std::shared_ptr<DeviceBuffer> FrameManager::EnqueuePostGraphicsImageReadback(const Texture &image, uint32_t array_layer, uint32_t miplevel) {
        auto texture_desc = image.GetTextureDescription();
        assert(array_layer <= texture_desc.array_layers);
        assert(miplevel <= texture_desc.mipmap_levels);
        // This has to be a shared pointer as release time is undetermined.
        std::shared_ptr staging_buffer = DeviceBuffer::CreateUnique(
            pimpl->m_system.GetAllocatorState(),
            {BufferTypeBits::ReadbackFromDevice},
            texture_desc.width * texture_desc.height * texture_desc.depth * ImageUtils::GetPixelSize(texture_desc.format)
        );

        auto enqueued = [=, &image] (vk::CommandBuffer cb) -> void {
            ReadbackCommand(
                cb, image,
                vk::ImageAspectFlagBits::eColor,
                miplevel, array_layer,
                vk::Extent3D{texture_desc.width, texture_desc.height, texture_desc.depth},
                *staging_buffer);
        };
        pimpl->readback.post_graphics_commands.push(enqueued);

        return staging_buffer;
    }
} // namespace Engine::RenderSystemState
