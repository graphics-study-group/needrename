#include "FrameManager.h"

#include "Rhi/DebugUtils.h"
#include "Rhi/DeviceInterface.h"
#include "Rhi/Structs.h"
#include "Rhi/ImageUtilsFunc.h"
#include "Rhi/DeviceBuffer.h"
#include "Render/Memory/MemoryAccessHelper.hpp"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/IPresentProvider.h"
#include "Rhi/SubmissionHelper.h"

#include "Render/RenderSystem/FrameSemaphore.hpp"

#include <SDL3/SDL.h>
#include <bitset>

namespace {
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

        // Frame completion credential: a binary semaphore per frame-in-flight,
        // signaled by the frame-completion batch and waited on by `Present`.
        // (Binary, because vkQueuePresentKHR accepts only binary semaphores.)
        std::array<vk::UniqueSemaphore, FRAMES_IN_FLIGHT> frame_completed_semaphores{};

        std::array<vk::UniqueFence, FRAMES_IN_FLIGHT> command_executed_fences{};

        std::array<vk::UniqueCommandBuffer, FRAMES_IN_FLIGHT> command_buffers{};

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
        IPresentProvider *m_present_provider = nullptr;

        std::unique_ptr<SubmissionHelper> m_submission_helper{};

        void assert_in_frame() const {
            if (current_framebuffer == std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("This method must be called between StartFrame and CompleteFrame");
            }
        }

        /// @brief Progress the frame state machine.
        void CompleteFrame();

        impl(RenderSystem &sys) : m_system(sys) {};
        void Create(IPresentProvider &present_provider);
    };

    FrameManager::FrameManager(RenderSystem &sys) : pimpl(std::make_unique<impl>(sys)) {
    }

    FrameManager::~FrameManager() = default;

    void FrameManager::impl::Create(IPresentProvider &present_provider) {
        m_present_provider = &present_provider;
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

            frame_completed_semaphores[i] = device.createSemaphoreUnique(scinfo);
            DEBUG_SET_NAME_TEMPLATE(
                device, frame_completed_semaphores[i].get(), std::format("Semaphore - frame completed {}", i)
            );

            command_executed_fences[i] = device.createFenceUnique(finfo);
            DEBUG_SET_NAME_TEMPLATE(
                device, command_executed_fences[i].get(), std::format("Fence - all commands executed {}", i)
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

        current_frame_in_flight = 0;
        m_submission_helper =
            std::make_unique<SubmissionHelper>(m_system.GetDeviceInterface(), m_system.GetAllocatorState());
    }

    void FrameManager::Create(IPresentProvider &present_provider) {
        pimpl->Create(present_provider);
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

    CommandBuffer FrameManager::BeginMainCommandBuffer() {
        pimpl->assert_in_frame();
        auto cb = GetRawMainCommandBuffer();
        cb.begin(vk::CommandBufferBeginInfo{});
        return CommandBuffer(pimpl->m_system, cb, GetFrameInFlight());
    }

    vk::CommandBuffer FrameManager::GetRawMainCommandBuffer() {
        return pimpl->command_buffers[GetFrameInFlight()].get();
    }

    uint32_t FrameManager::StartFrame(uint64_t timeout) {
        auto device = pimpl->m_system.GetDevice();
        uint32_t fif = GetFrameInFlight();

        // Acquire FIRST (async, never blocks). On failure the frame state is
        // left untouched: `current_framebuffer` keeps its previous value, the
        // fence is NOT reset and the FIF is NOT advanced, so the caller can
        // safely skip this frame (or recreate the swapchain and retry) without
        // deadlocking — the fence stays signaled and no wait depends on this
        // frame's submit.
        auto result = pimpl->m_present_provider->AcquireNextImage(
            pimpl->m_system.GetDevice(), pimpl->image_acquired_semaphores[fif].get(), timeout
        );
        if (result == std::numeric_limits<uint32_t>::max()) {
            return std::numeric_limits<uint32_t>::max();
        }
        pimpl->current_framebuffer = result;

        // CPU throttle: wait for the previous frame on this FIF to finish,
        // then reset its resources. Must happen after a successful acquire so
        // an out-of-date retry never waits on a fence that no submit will
        // signal (fences are only signaled by a completed submit, and
        // `waitIdle` does not signal them).
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

        return pimpl->current_framebuffer;
    }

    bool FrameManager::SubmitFrame(const RenderTargetTexture &present_texture, MemoryAccessTypeImageBits last_access) {
        pimpl->assert_in_frame();

        const uint32_t fif = GetFrameInFlight();
        auto &this_timeline_semaphore = pimpl->timeline_semaphores[fif];
        auto &prev_timeline_semaphore = pimpl->timeline_semaphores[(fif + (FRAMES_IN_FLIGHT - 1)) % FRAMES_IN_FLIGHT];
        // Timeline timepoints: 1 = start (validation silence), 2 = staged
        // upload complete, 4 = frame complete. Timepoint 3 is gone: the copy
        // executes in-order inside the same batch, so no "render complete"
        // wait is needed. The value jumps 2 -> 4, which timeline semaphores
        // allow.
        this_timeline_semaphore.SetExpectedTimepoints(4);

        // End the main command buffer (recording is driven by the render layer,
        // but the lifecycle belongs to FrameManager).
        pimpl->command_buffers[fif]->end();

        // Staged resource submission (signals timeline timepoint 2).
        pimpl->m_submission_helper->ExecuteSubmission(this_timeline_semaphore.GetSignalInfo(2));

        // Record the copy CB (headless → nullptr; the batch then carries no copy).
        auto copy_cb = pimpl->m_present_provider->PrepareCopy(
            pimpl->m_system.GetDevice(), present_texture, GetFramebuffer(), last_access
        );

        // ── One frame-completion batch: {main render CB, copy CB} ──
        std::array<vk::SemaphoreSubmitInfo, 3> wait_infos{};
        wait_infos[0] = this_timeline_semaphore.GetSubmitInfo(
            2,
            // Wait for staged upload before any command starts.
            vk::PipelineStageFlagBits2::eAllCommands
        );
        // Wait for total completion of the last frame.
        wait_infos[1] = prev_timeline_semaphore.GetSubmitInfo(
            prev_timeline_semaphore.GetExpectedTimepoints(), vk::PipelineStageFlagBits2::eAllCommands
        );
        // Wait for image acquisition. eAllTransfer = a stage latch, NOT a batch
        // latch: graphics/compute stages (earlier than eTransfer) may proceed
        // before the image is ready; only transfer-stage commands (the copy
        // blit) are gated. Must not use eAllCommands here.
        wait_infos[2] = vk::SemaphoreSubmitInfo{
            pimpl->image_acquired_semaphores[fif].get(), 0, vk::PipelineStageFlagBits2::eAllTransfer
        };

        // Special consideration for deadlock on the first frame.
        if (GetTotalFrame() == 0) {
            prev_timeline_semaphore.SetExpectedTimepoints(1);
            this->pimpl->m_system.GetDevice().signalSemaphore(
                prev_timeline_semaphore.GetSignalInfo(prev_timeline_semaphore.GetExpectedTimepoints())
            );
        }
        // We must step frame after wait info is recorded to avoid deadlock.
        prev_timeline_semaphore.EndFrame();

        std::array<vk::SemaphoreSubmitInfo, 2> signal_infos{};
        // Frame complete (timeline).
        signal_infos[0] = this_timeline_semaphore.GetSubmitInfo(
            4,
            // Signal after all commands (render + copy) are finished.
            vk::PipelineStageFlagBits2::eAllCommands
        );
        // Frame completion credential (binary) — waited on by Present.
        signal_infos[1] = vk::SemaphoreSubmitInfo{
            pimpl->frame_completed_semaphores[fif].get(), 0, vk::PipelineStageFlagBits2::eAllCommands
        };

        vk::CommandBufferSubmitInfo cbs[]{{pimpl->command_buffers[fif].get()}, {copy_cb}};
        const uint32_t cb_count = copy_cb ? 2u : 1u;
        vk::SubmitInfo2 sinfo{};
        sinfo.waitSemaphoreInfoCount = static_cast<uint32_t>(wait_infos.size());
        sinfo.pWaitSemaphoreInfos = wait_infos.data();
        sinfo.commandBufferInfoCount = cb_count;
        sinfo.pCommandBufferInfos = cbs;
        sinfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signal_infos.size());
        sinfo.pSignalSemaphoreInfos = signal_infos.data();
        pimpl->m_system.GetDeviceInterface().GetQueueInfo().graphicsQueue.submit2(
            sinfo, pimpl->command_executed_fences[fif].get()
        );

        // Present (waits on the frame completion credential).
        bool needs_recreating = pimpl->m_present_provider->Present(
            pimpl->m_system.GetDevice(), GetFramebuffer(), pimpl->frame_completed_semaphores[fif].get()
        );

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
