#include "FrameManager.h"

#include "Render/Pipeline/CommandBuffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/IPresentProvider.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/Fence.h"
#include "Rhi/Device/Semaphore.h"
#include "Rhi/Command/CommandBuffer.h"
#include "Rhi/Submission/EpochTracker.h"
#include "Rhi/Submission/SubmissionHelper.h"

#include "Render/RenderSystem/FrameSemaphore.hpp"

#include <SDL3/SDL.h>
#include <unordered_map>
#include <bitset>

namespace {
    void ReadbackCommand(
        Engine::Rhi::CommandBufferHandle cb, const Engine::Rhi::DeviceBuffer &src, const Engine::Rhi::DeviceBuffer &dst
    ) {
        assert(src.GetSize() == dst.GetSize());
        // The copy is ordinary; only the command buffer it lands in is special,
        // because this recording is submitted on its own fence.
        Engine::Rhi::CommandEncoder encoder{cb};
        encoder.CopyBuffer(src.GetBuffer(), dst.GetBuffer(), dst.GetSize());
    }
} // namespace

namespace Engine::RenderSystemState {
    struct FrameManager::impl {

        std::array<FrameSemaphore, FRAMES_IN_FLIGHT> timeline_semaphores{};

        // One encoder per frame-in-flight. The render command buffer borrows it
        // for the frame, because a second encoder over the same command buffer
        // would track its own state and disagree about push constants.
        std::array<std::unique_ptr<Rhi::CommandEncoder>, FRAMES_IN_FLIGHT> main_encoders{};


        std::array<std::optional<Rhi::Semaphore>, FRAMES_IN_FLIGHT> image_acquired_semaphores{};

        // Frame completion credential: one binary semaphore *per swapchain
        // image*, signaled by the frame-completion batch and waited on by
        // `Present` (binary, because vkQueuePresentKHR accepts only binary
        // semaphores).
        //
        // Per image, not per frame-in-flight: a binary present semaphore may be
        // reused only after the presentation operation that waited on it has
        // completed. That is exactly what re-acquiring the image guarantees,
        // but a frame-in-flight slot would be reused with a *different* image
        // whose present may still be pending — a signal-while-signaled
        // violation (VUID-vkQueueSubmit2-semaphore-03868).
        std::vector<Rhi::Semaphore> frame_completed_semaphores{};

std::array<std::optional<Rhi::Fence>, FRAMES_IN_FLIGHT> command_executed_fences{};

std::array<std::optional<Rhi::CommandBuffer>, FRAMES_IN_FLIGHT> command_buffers{};

        // Data and handles used by readback routines.
        struct {
            struct ReadbackRegistry {
                // TODO: reuse fences and command buffers to avoid frequent reallocation.
                std::optional<Rhi::Fence> fence;
                std::optional<Rhi::CommandBuffer> combuf;
                std::deque<std::pair<ReadbackCallback, std::unique_ptr<Rhi::DeviceBuffer>>> callbacks;
            };

            ReadbackRegistry current_registry;

            std::deque<ReadbackRegistry> registry;

            bool HasReadback() const {
                return static_cast<bool>(current_registry.fence);
            }

            void InitializeRegistry(const Rhi::DeviceInterface &di, const Rhi::DeviceContext &device_context) {
                assert(!current_registry.fence && "Reinitializing readback registry");
                current_registry.fence = Rhi::Fence::Create(di, false);

                auto onetime_buffers =
                    Rhi::CommandBuffer::Create(device_context, Rhi::CommandBufferPool::GraphicsOneTime, 1);
                current_registry.combuf = std::move(onetime_buffers[0]);
                current_registry.combuf->Begin();
            }

            /// Staging buffers whose callback has run, ready to be filled again.
            std::unordered_map<size_t, std::vector<std::unique_ptr<Rhi::DeviceBuffer>>> pool;

            /// Take a buffer of `size` bytes from the pool, or make one.
            std::unique_ptr<Rhi::DeviceBuffer> TakeBuffer(const Rhi::AllocatorState &allocator, size_t size) {
                auto found = pool.find(size);
                if (found != pool.end() && !found->second.empty()) {
                    std::unique_ptr<Rhi::DeviceBuffer> buffer = std::move(found->second.back());
                    found->second.pop_back();
                    return buffer;
                }
                return Rhi::DeviceBuffer::CreateUnique(
                    allocator, Rhi::BufferType{Rhi::BufferTypeBits::ReadbackFromDevice}, size
                );
            }

            /// Give a buffer back once its callback has run.
            void ReturnBuffer(std::unique_ptr<Rhi::DeviceBuffer> buffer) {
                if (buffer != nullptr) {
                    pool[buffer->GetSize()].push_back(std::move(buffer));
                }
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

        // The frame epoch opened for each in-flight slot, reported when that
        // slot's command-executed fence is next waited on. INVALID means the
        // slot has no open epoch (nothing submitted yet, or a skipped frame).
        std::array<Rhi::EpochWatermark, FRAMES_IN_FLIGHT> frame_epochs{};

        // Last frame-completion fence, captured at submit time (before
        // CompleteFrame advances the FIF counter). Null until the first submit.
        Rhi::FenceHandle m_last_submitted_fence{};
        bool m_has_submitted = false;

        RenderSystem &m_system;
        IPresentProvider *m_present_provider = nullptr;

        std::unique_ptr<Rhi::SubmissionHelper> m_submission_helper{};

        void assert_in_frame() const {
            if (current_framebuffer == std::numeric_limits<uint32_t>::max()) {
                throw std::runtime_error("This method must be called between StartFrame and CompleteFrame");
            }
        }

        /// @brief (Re)create one present semaphore per presentation image.
        void CreateFrameCompletedSemaphores(uint32_t image_count);

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


        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            image_acquired_semaphores[i] = Rhi::Semaphore::Create(
                m_system.GetDeviceInterface(), Rhi::SemaphoreType::Binary, 0, std::format("Semaphore - image acquired {}", i)
            );




            command_executed_fences[i] =
                Rhi::Fence::Create(m_system.GetDeviceInterface(), true, std::format("Fence - all commands executed {}", i));
        }

        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            timeline_semaphores[i].SetSemaphore(Rhi::Semaphore::Create(
                m_system.GetDeviceInterface(),
                Rhi::SemaphoreType::Timeline,
                0,
                std::format("Semaphore - timeline semaphore {}", i)
            ));



        }

        // Allocate main render command buffers
        const auto &queue_info = m_system.GetDeviceInterface().GetQueueInfo();
        auto main_command_buffers = Rhi::CommandBuffer::Create(
            m_system.GetDeviceContext(), Rhi::CommandBufferPool::Graphics, FRAMES_IN_FLIGHT
        );
        for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
            command_buffers[i] = std::move(main_command_buffers[i]);
        }

        current_frame_in_flight = 0;
        CreateFrameCompletedSemaphores(present_provider.GetImageCount());
        m_submission_helper = std::make_unique<Rhi::SubmissionHelper>(
            m_system.GetDeviceInterface(), m_system.GetAllocatorState(), m_system.GetDeviceContext().GetEpochTracker()
        );
    }

    void FrameManager::impl::CreateFrameCompletedSemaphores(uint32_t image_count) {
        // Each of these is waited on by exactly one present operation of its
        // image, and re-acquiring that image proves that wait executed, so the
        // semaphore is unsignaled when it is signaled again.
        frame_completed_semaphores.clear();
        if (!m_present_provider->NeedsPresentSemaphore()) {
            // Nothing would consume them (headless): an unconsumed binary
            // semaphore stays signaled and could never be signaled again.
            return;
        }

        frame_completed_semaphores.reserve(image_count);
        for (uint32_t i = 0; i < image_count; i++) {
            frame_completed_semaphores.push_back(Rhi::Semaphore::Create(
                m_system.GetDeviceInterface(),
                Rhi::SemaphoreType::Binary,
                0,
                std::format("Semaphore - frame completed image {}", i)
            ));



        }
    }

    void FrameManager::OnPresentTargetRecreated() {
        // A semaphore waited on by a present of the old swapchain must not be
        // signaled again before that wait executed. Draining the device bounds
        // the wait: no submission of ours is pending any more.
        // Draining through the seam rather than the Vulkan device: the latter is what kept this
        // shared file from compiling for the other backend.
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
        const uint32_t frame = GetFrameInFlight();
        pimpl->command_buffers[frame]->Begin();
        // Reuse the frame slot's encoder rather than building a fresh one: a later
        // RecordIntoMainCommandBuffer borrows the same object.
        Rhi::CommandEncoder &encoder = GetMainEncoder();
        return CommandBuffer(pimpl->m_system, encoder, frame);
    }

    Rhi::CommandEncoder &FrameManager::GetMainEncoder() {
        const uint32_t frame = GetFrameInFlight();
        if (!pimpl->main_encoders[frame]) {
            pimpl->main_encoders[frame] =
std::make_unique<Rhi::CommandEncoder>(GetRawMainCommandBuffer());
        }
        return *pimpl->main_encoders[frame];
    }

    Rhi::CommandBufferHandle FrameManager::GetRawMainCommandBuffer() {
        return pimpl->command_buffers[GetFrameInFlight()]->GetHandle();
    }

    uint32_t FrameManager::StartFrame(uint64_t timeout) {
        uint32_t fif = GetFrameInFlight();

        // CPU throttle: wait for the previous frame on this FIF to finish.
        // MUST happen before acquire: the acquire semaphore was waited on by
        // the previous frame's submit, and reusing it while that wait is still
        // pending violates VUID-vkAcquireNextImageKHR-semaphore-01779.
        if (!pimpl->m_system.GetDeviceContext().WaitForFence(
                pimpl->command_executed_fences[fif]->GetHandle(), timeout
            )) {
            throw std::runtime_error("Timed out waiting for the frame fence.");
        }

        // The fence of the frame that last used this slot has been observed, so
        // that frame's epoch can be reported. This is the frame epoch's only
        // reporting point, and it is why the completed prefix lags by roughly
        // the in-flight depth.
        if (pimpl->frame_epochs[fif] != Rhi::INVALID_EPOCH_WATERMARK) {
            pimpl->m_system.GetDeviceContext().GetEpochTracker().ReportComplete(pimpl->frame_epochs[fif]);
            pimpl->frame_epochs[fif] = Rhi::INVALID_EPOCH_WATERMARK;
        }

        // Acquire (async, never blocks). On failure the frame state is left
        // untouched: `current_framebuffer` keeps its previous value, the fence
        // is NOT reset and the FIF is NOT advanced, so the caller can safely
        // skip this frame (or recreate the swapchain and retry) without
        // deadlocking — the fence stays signaled and no wait depends on this
        // frame's submit.
        auto result = pimpl->m_present_provider->AcquireNextImage(
            pimpl->m_system.GetDeviceInterface().GetDevice(), pimpl->image_acquired_semaphores[fif]->GetHandle(), timeout
        );
        if (result == std::numeric_limits<uint32_t>::max()) {
            return std::numeric_limits<uint32_t>::max();
        }
        pimpl->current_framebuffer = result;

        // Acquire succeeded: the fence is guaranteed signaled (waited above),
        // so resetting the command buffer and fence here cannot deadlock a
        // later wait (fences are only signaled by a completed submit, and
        // `waitIdle` does not signal them).
        pimpl->command_buffers[fif]->Reset();
        pimpl->command_executed_fences[fif]->Reset();
        // The reset replaced the recording object this slot records into - a new encoder on a
        // backend that finishes one into a command buffer - so the cached encoder is stale from
        // here on. Rebuilding it is part of the reset rather than a separate concern: recording
        // into the previous one targets an encoder that no longer exists.
        pimpl->main_encoders[fif] = std::make_unique<Rhi::CommandEncoder>(pimpl->command_buffers[fif]->GetHandle());

        // Kickstart of this frame
        // Prevent validation layer from complaining
        if (pimpl->timeline_semaphores[fif].GetTotalElapsedTimepoints() > 0) {
            pimpl->m_system.GetDeviceContext().SignalSemaphore(
                Rhi::TimelineSignal{
                    pimpl->timeline_semaphores[fif].GetSemaphore(),
                    pimpl->timeline_semaphores[fif].GetTimepointValue(1)
                }
            );
        }

        // The frame is committed: acquisition succeeded and the command buffer
        // is reset, so open the frame's epoch here. Opening it before acquisition
        // would create an epoch whose submission never happens when the frame is
        // skipped, stranding everything parked under it (and everything above it,
        // since the completed prefix cannot jump the gap).
        pimpl->frame_epochs[fif] = pimpl->m_system.GetDeviceContext().GetEpochTracker().BeginEpoch();

        return pimpl->current_framebuffer;
    }

    bool FrameManager::SubmitFrame(
        const RenderTargetTexture &present_texture, Rhi::MemoryAccessTypeImageBits last_access
    ) {
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
        pimpl->command_buffers[fif]->End();

        // Staged resource submission (signals timeline timepoint 2).
        pimpl->m_submission_helper->ExecuteSubmission(
            Rhi::TimelineSignal{this_timeline_semaphore.GetSemaphore(), this_timeline_semaphore.GetTimepointValue(2)}
        );

        // Record the copy CB (headless → nullptr; the batch then carries no copy).
        const uint32_t image_index = GetFramebuffer();
        const Rhi::CommandBufferHandle copy_cb = pimpl->m_present_provider->PrepareCopy(
            pimpl->m_system.GetDeviceInterface().GetDevice(), present_texture, image_index, last_access
        );

        // ── One frame-completion batch: {main render CB, copy CB} ──
        // ── One frame-completion batch: {main render CB, copy CB} ──
        Rhi::Submission submission{};
        submission.command_buffers.push_back(pimpl->command_buffers[fif]->GetHandle());
        if (copy_cb.IsValid()) {
            submission.command_buffers.push_back(copy_cb);
        }

        // Wait for staged upload before any command starts.
        submission.waits.push_back(Rhi::SubmissionDependency{
            this_timeline_semaphore.GetSemaphore(),
            this_timeline_semaphore.GetTimepointValue(2),
            Rhi::PipelineScope::Unspecified
        });
        // Wait for total completion of the last frame.
        submission.waits.push_back(Rhi::SubmissionDependency{
            prev_timeline_semaphore.GetSemaphore(),
            prev_timeline_semaphore.GetExpectedTimepoints(),
            Rhi::PipelineScope::Unspecified
        });
        // Wait for image acquisition. Transfer scope is a stage latch, NOT a batch
        // latch: graphics and compute work may proceed before the image is ready;
        // only the copy blit is gated. Widening this to everything would serialize
        // the frame behind the acquire.
        submission.waits.push_back(Rhi::SubmissionDependency{
            pimpl->image_acquired_semaphores[fif]->GetHandle(),
            0,
            Rhi::PipelineScope::Transfer
        });

        // Special consideration for deadlock on the first frame.
        if (GetTotalFrame() == 0) {
            prev_timeline_semaphore.SetExpectedTimepoints(1);
            this->pimpl->m_system.GetDeviceContext().SignalSemaphore(
                Rhi::TimelineSignal{
                    prev_timeline_semaphore.GetSemaphore(),
                    prev_timeline_semaphore.GetTimepointValue(1)
                }
            );
        }
        // We must step frame after wait info is recorded to avoid deadlock.
        prev_timeline_semaphore.EndFrame();

        // Frame complete (timeline), after both command buffers.
        submission.signals.push_back(Rhi::SubmissionDependency{
            this_timeline_semaphore.GetSemaphore(),
            this_timeline_semaphore.GetTimepointValue(4),
            Rhi::PipelineScope::Unspecified
        });
        // Frame completion credential (binary) — waited on by Present. Indexed
        // by the acquired image, so the semaphore is only reused together with
        // its image, i.e. after the present that waited on it has completed.
        if (pimpl->m_present_provider->NeedsPresentSemaphore()) {
            assert(image_index < pimpl->frame_completed_semaphores.size());
            submission.signals.push_back(Rhi::SubmissionDependency{
                pimpl->frame_completed_semaphores[image_index].GetHandle(),
                0,
                Rhi::PipelineScope::Unspecified
            });
        }

        pimpl->m_system.GetDeviceContext().Submit(
            submission, pimpl->command_executed_fences[fif]->GetHandle()
        );
        // Capture the fence BEFORE CompleteFrame advances the FIF counter, so a
        // later WaitForFrameCompletion() targets the just-submitted frame.
        pimpl->m_last_submitted_fence = pimpl->command_executed_fences[fif]->GetHandle();
        pimpl->m_has_submitted = true;

        // Present (waits on the frame completion credential).
        const Rhi::SemaphoreHandle frame_done_semaphore = pimpl->m_present_provider->NeedsPresentSemaphore()
                                                             ? pimpl->frame_completed_semaphores[image_index].GetHandle()
                                                             : Rhi::SemaphoreHandle{};
        bool needs_recreating = pimpl->m_present_provider->Present(
            pimpl->m_system.GetDeviceInterface().GetDevice(), image_index, frame_done_semaphore
        );

        pimpl->CompleteFrame();
        return needs_recreating;
    }

    void FrameManager::impl::CompleteFrame() {
        // Record current readbacks.
        if (readback.HasReadback()) {
            readback.current_registry.combuf->End();
            // Submit this commandbuffer, waiting for the frame's last timepoint.
            auto &this_frame_semaphore = timeline_semaphores[current_frame_in_flight];
            Rhi::Submission readback_submission{};
            readback_submission.command_buffers.push_back(
                readback.current_registry.combuf->GetHandle()
            );
            readback_submission.waits.push_back(Rhi::SubmissionDependency{
                this_frame_semaphore.GetSemaphore(),
                this_frame_semaphore.GetExpectedTimepoints(),
                Rhi::PipelineScope::Unspecified
            });
            m_system.GetDeviceContext().Submit(
                readback_submission, readback.current_registry.fence->GetHandle()
            );

            readback.AddRegistery();
        }

        // call previous readbacks.
        while (!readback.registry.empty()) {
            auto &fnt = readback.registry.front();
            auto &device_context = m_system.GetDeviceContext();
            const bool signaled = device_context.WaitForFence(fnt.fence->GetHandle(), 0);
            if (!signaled) break;

            for (auto &itr : fnt.callbacks) {
                // The buffer has to be addressable before its callback reads it, and this is the
                // one point where that can be waited for: the pump is not inside the device's
                // own event dispatch here.
                itr.second->PrepareReadback();
                std::invoke(itr.first, *itr.second);
                // The callback borrowed the buffer; it goes back to the pool for the next frame
                // rather than being destroyed.
                readback.ReturnBuffer(std::move(itr.second));
            }

            readback.registry.pop_front();
        }

        // Increment FIF counter, reset framebuffer index
        current_frame_in_flight = (current_frame_in_flight + 1) % FRAMES_IN_FLIGHT;
        current_framebuffer = std::numeric_limits<uint32_t>::max();
        total_frame_count++;

        // Handle submissions
        m_submission_helper->OnBatchComplete();
    }

    Rhi::SubmissionHelper &FrameManager::GetSubmissionHelper() {
        return *(pimpl->m_submission_helper);
    }

    void FrameManager::WaitForFrameCompletion() const {
        if (!pimpl->m_has_submitted || !pimpl->m_last_submitted_fence.IsValid()) {
            return;
        }
        if (!pimpl->m_system.GetDeviceContext().WaitForFence(
                pimpl->m_last_submitted_fence, std::numeric_limits<uint64_t>::max()
            )) {
            throw std::runtime_error("Timed out waiting for frame completion.");
        }
    }
    const FrameSemaphore &FrameManager::GetFrameSemaphore() const noexcept {
        return pimpl->timeline_semaphores[GetFrameInFlight()];
    }

    bool FrameManager::RegisterReadbackCallback(const Rhi::DeviceBuffer &buffer, ReadbackCallback cb) {
        if (pimpl->readback.registry.size() >= FRAMES_IN_FLIGHT) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Too many uncalled callback registry. New request is ignored.");
            return false;
        }

        if (!pimpl->readback.HasReadback()) {
        pimpl->readback.InitializeRegistry(pimpl->m_system.GetDeviceInterface(), pimpl->m_system.GetDeviceContext());
        }

        // A read-back staging buffer is borrowed from the pool and returned when its callback has
        // run. A test that reads a frame back every frame would otherwise allocate (and hold) a
        // frame-sized buffer per frame, and the allocator is where that ends up spinning.
        std::unique_ptr<Rhi::DeviceBuffer> staging_buffer = pimpl->readback.TakeBuffer(
            pimpl->m_system.GetAllocatorState(), buffer.GetSize()
        );
        if (staging_buffer == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU surface: the read-back staging buffer could not be created.");
            return false;
        }

        ReadbackCommand(pimpl->readback.current_registry.combuf->GetHandle(), buffer, *staging_buffer);
        pimpl->readback.current_registry.callbacks.push_back(std::make_pair(cb, std::move(staging_buffer)));
        return true;
    }
} // namespace Engine::RenderSystemState

















