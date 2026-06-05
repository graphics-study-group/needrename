#include "FrameManager.h"

#include "Render/DebugUtils.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/Memory/DeviceBuffer.h"
#include "Render/Memory/MemoryAccessHelper.hpp"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include "Render/RenderSystem/Structs.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Render/RenderSystem/Swapchain.h"

#include "Render/RenderSystem/FrameSemaphore.hpp"

#include <SDL3/SDL.h>
#include <bitset>

namespace {
    void RecordCopyCommand(
        const vk::CommandBuffer &cb,
        const vk::Image &src,
        Engine::MemoryAccessTypeImageBits last_access,
        vk::Extent2D extent_src,
        vk::Offset2D offset_src,
        vk::Extent2D extent_dst,
        vk::Offset2D offset_dst,
        const Engine::RenderSystemState::Swapchain &swapchain,
        uint32_t framebuffer,
        vk::Filter filter
    ) {
        std::array<vk::ImageMemoryBarrier2, 2> barriers{};

        cb.begin(vk::CommandBufferBeginInfo{});
        DEBUG_CMD_START_LABEL(cb, "Final Copy");
        barriers[0] = vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eAllCommands,
            Engine::GetAccessFlags({last_access}),
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferRead,
            Engine::GetImageLayout({last_access}),
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
                 vk::Offset3D{offset_src.x + (int32_t)extent_src.width, offset_src.y + (int32_t)extent_src.height, 1}},
                vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {vk::Offset3D{offset_dst, 0},
                 vk::Offset3D{offset_dst.x + (int32_t)extent_dst.width, offset_dst.y + (int32_t)extent_dst.height, 1}}
            }},
            filter
        );

        barriers[0] = vk::ImageMemoryBarrier2{
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferRead,
            vk::PipelineStageFlagBits2::eNone,
            vk::AccessFlagBits2::eNone,
            // No layout transitions here.
            vk::ImageLayout::eTransferSrcOptimal,
            vk::ImageLayout::eTransferSrcOptimal,
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

    void ReadbackCommand(vk::CommandBuffer cb, const Engine::DeviceBuffer &src, const Engine::DeviceBuffer &dst) {
        using namespace Engine;
        assert(src.GetSize() == dst.GetSize());
        cb.copyBuffer(src.GetBuffer(), dst.GetBuffer(), vk::BufferCopy{0, 0, dst.GetSize()});
    }
} // namespace

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
            struct ReadbackRegistry {
                // TODO: reuse fences and command buffers to avoid frequent reallocation.
                vk::UniqueFence fence;
                vk::UniqueCommandBuffer combuf;
                std::deque<std::pair<ReadbackCallback, std::unique_ptr<DeviceBuffer>>> callbacks;
            };

            ReadbackRegistry current_registry;

            std::deque<ReadbackRegistry> registry;

            bool HasReadback() const {
                return static_cast<bool>(current_registry.fence);
            }

            void InitializeRegistry(const RenderSystemState::DeviceInterface &di) {
                assert(!current_registry.fence && "Reinitializing readback registry");
                current_registry.fence = di.GetDevice().createFenceUnique(vk::FenceCreateInfo{});

                auto cbai = vk::CommandBufferAllocateInfo{
                    di.GetQueueInfo().graphicsOneTimePool.get(), vk::CommandBufferLevel::ePrimary, 1
                };
                auto onetime_cb = di.GetDevice().allocateCommandBuffersUnique(cbai);
                current_registry.combuf = std::move(onetime_cb[0]);
                current_registry.combuf.get().begin(
                    vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit}
                );
            }

            void AddRegistery() {
                registry.push_back(std::move(current_registry));

                current_registry.fence.reset();
                current_registry.combuf.reset();
                current_registry.callbacks.clear();
            }
        } readback{};

        uint32_t current_frame_in_flight{std::numeric_limits<uint32_t>::max()};

        uint32_t current_framebuffer{std::numeric_limits<uint32_t>::max()};

        uint64_t total_frame_count{0};

        RenderSystem &m_system;

        std::unique_ptr<SubmissionHelper> m_submission_helper{};

        void assert_in_frame() const {
            if (current_framebuffer == std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("This method must be called between StartFrame and CompleteFrame");
            }
        }

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
            timeline_semaphores[i].SetSemaphore(device.createSemaphoreUnique(scinfo));
            DEBUG_SET_NAME_TEMPLATE(
                device, timeline_semaphores[i].GetSemaphore(), std::format("Semaphore - timeline semaphore {}", i)
            );
        }

        // Allocate main render command buffers
        const auto &queue_info = m_system.GetDeviceInterface().GetQueueInfo();
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
        pimpl->assert_in_frame();
        return this->pimpl->current_framebuffer;
    }

    CommandBuffer FrameManager::GetCommandBuffer() {
        pimpl->assert_in_frame();
        return CommandBuffer(pimpl->m_system, GetRawMainCommandBuffer(), GetFrameInFlight());
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
        if (wait_result != vk::Result::eSuccess) {
            throw std::runtime_error(vk::to_string(wait_result) + " happened when waiting for frame fences.");
        }
        pimpl->command_buffers[fif]->reset();
        device.resetFences({fence});

        // Kickstart of this frame
        // Prevent validation layer from complaining
        if (pimpl->timeline_semaphores[fif].GetTotalElapsedTimepoints() > 0) {
            device.signalSemaphore(pimpl->timeline_semaphores[fif].GetSignalInfo(1));
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
        pimpl->assert_in_frame();

        const uint32_t fif = GetFrameInFlight();
        auto &this_timeline_semaphore = pimpl->timeline_semaphores[fif];
        auto &prev_timeline_semaphore = pimpl->timeline_semaphores[(fif + (FRAMES_IN_FLIGHT - 1)) % FRAMES_IN_FLIGHT];
        // TODO: we currently hardcode expected timepoints to be 4, namely: start(1), transfer(2), render(3) and presenting(4).
        // This start timepoint is not actually needed other than scilencing the validation layer.
        // The presenting timepoint is currently not needed actually as all operations happen on one queue.
        this_timeline_semaphore.SetExpectedTimepoints(4);

        pimpl->m_submission_helper->OnPreMainCbSubmission();

        vk::CommandBufferSubmitInfo cbsi{pimpl->command_buffers[fif].get()};
        std::array<vk::SemaphoreSubmitInfo, 2> wait_infos{};
        vk::SemaphoreSubmitInfo signal_info{};
        wait_infos[0] = this_timeline_semaphore.GetSubmitInfo(
            2,
            // Wait before any command starts.
            vk::PipelineStageFlagBits2::eAllCommands
        );
        // Wait for total completion of the last frame
        wait_infos[1] = prev_timeline_semaphore.GetSubmitInfo(
            prev_timeline_semaphore.GetExpectedTimepoints(), vk::PipelineStageFlagBits2::eAllCommands
        );
        // special consideration for deadlock on the first frame.
        if (GetTotalFrame() == 0) {
            prev_timeline_semaphore.SetExpectedTimepoints(1);
            this->pimpl->m_system.GetDevice().signalSemaphore(
                prev_timeline_semaphore.GetSignalInfo(prev_timeline_semaphore.GetExpectedTimepoints())
            );
        }
        // We must step frame after wait info is recorded to avoid deadlock.
        prev_timeline_semaphore.EndFrame();

        signal_info = this_timeline_semaphore.GetSubmitInfo(
            3,
            // Signal after all commands are finished.
            vk::PipelineStageFlagBits2::eAllCommands
        );

        this->pimpl->m_system.GetDeviceInterface().GetQueueInfo().graphicsQueue.submit2(
            vk::SubmitInfo2{vk::SubmitFlags{}, wait_infos, {cbsi}, {signal_info}}, nullptr
        );
    }

    bool FrameManager::PresentToFramebuffer(
        vk::Image image,
        MemoryAccessTypeImageBits last_access,
        vk::Extent2D extentSrc,
        vk::Offset2D offsetSrc,
        vk::Filter filter
    ) {
        pimpl->assert_in_frame();

        const auto fif = GetFrameInFlight();
        const auto &copy_cb = pimpl->copy_to_swapchain_command_buffers[fif].get();

        RecordCopyCommand(
            copy_cb,
            image,
            last_access,
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
        std::array<vk::SemaphoreSubmitInfo, 2> wait_infos{};
        std::array<vk::SemaphoreSubmitInfo, 2> signal_infos{};

        // Wait for the second-to-last timepoint
        auto &this_frame_semaphore = pimpl->timeline_semaphores[fif];
        wait_infos[0] = this_frame_semaphore.GetSubmitInfo(
            this_frame_semaphore.GetExpectedTimepoints() - 1, vk::PipelineStageFlagBits2::eAllTransfer
        );
        // Wait for image acquisition (this is binary).
        wait_infos[1] = vk::SemaphoreSubmitInfo{
            pimpl->image_acquired_semaphores[fif].get(), 0, vk::PipelineStageFlagBits2::eAllTransfer
        };

        // Signal ready for presenting.
        signal_infos[0] = vk::SemaphoreSubmitInfo{
            pimpl->copy_to_swapchain_completed_semaphores[GetFramebuffer()].get(),
            0,
            vk::PipelineStageFlagBits2::eAllTransfer
        };
        signal_infos[1] = this_frame_semaphore.GetSubmitInfo(
            this_frame_semaphore.GetExpectedTimepoints(), vk::PipelineStageFlagBits2::eAllTransfer
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

                // Suboptimal swapchain should be recreated after presenting.
                if (result == vk::Result::eSuboptimalKHR) {
                    needs_recreating = true;
                }
            }
        } catch (vk::OutOfDateKHRError &e) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Swapchain out of date.");
            needs_recreating = true;
        }

        pimpl->CompleteFrame();
        return needs_recreating;
    }

    void FrameManager::impl::CompleteFrame() {
        // Record current readbacks.
        if (readback.HasReadback()) {
            readback.current_registry.combuf->end();
            // Submit this commandbuffer
            vk::CommandBufferSubmitInfo cbsi{readback.current_registry.combuf.get()};
            std::array<vk::SemaphoreSubmitInfo, 1> wait_infos{};

            // Wait for the last timepoint
            auto &this_frame_semaphore = timeline_semaphores[current_frame_in_flight];
            wait_infos[0] = this_frame_semaphore.GetSubmitInfo(
                this_frame_semaphore.GetExpectedTimepoints(), vk::PipelineStageFlagBits2::eAllCommands
            );

            const auto &gqueue = m_system.GetDeviceInterface().GetQueueInfo().graphicsQueue;
            gqueue.submit2(
                {vk::SubmitInfo2{vk::SubmitFlags{}, wait_infos, {cbsi}, {}}}, readback.current_registry.fence.get()
            );

            readback.AddRegistery();
        }

        // call previous readbacks.
        while (!readback.registry.empty()) {
            auto &fnt = readback.registry.front();
            auto ret = m_system.GetDevice().getFenceStatus(fnt.fence.get());
            if (ret == vk::Result::eNotReady) break;
            if (ret != vk::Result::eSuccess) {
                throw std::runtime_error(vk::to_string(ret) + " happened when querying status of readback fence.");
            }

            for (auto &itr : fnt.callbacks) {
                std::invoke(itr.first, std::move(itr.second));
            }

            readback.registry.pop_front();
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
    const FrameSemaphore &FrameManager::GetFrameSemaphore() const noexcept {
        return pimpl->timeline_semaphores[GetFrameInFlight()];
    }

    bool FrameManager::RegisterReadbackCallback(const DeviceBuffer &buffer, ReadbackCallback cb) {
        if (pimpl->readback.registry.size() >= FRAMES_IN_FLIGHT) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Too many uncalled callback registry. New request is ignored.");
            return false;
        }

        if (!pimpl->readback.HasReadback()) {
            pimpl->readback.InitializeRegistry(pimpl->m_system.GetDeviceInterface());
        }

        auto staging_buffer = DeviceBuffer::CreateUnique(
            pimpl->m_system.GetAllocatorState(), BufferType{BufferTypeBits::ReadbackFromDevice}, buffer.GetSize()
        );

        ReadbackCommand(pimpl->readback.current_registry.combuf.get(), buffer, *staging_buffer);
        pimpl->readback.current_registry.callbacks.push_back(std::make_pair(cb, std::move(staging_buffer)));
        return true;
    }
} // namespace Engine::RenderSystemState
