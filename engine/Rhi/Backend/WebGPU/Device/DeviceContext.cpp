#include "Rhi/Device/DeviceContext.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/Fence.h"
#include "Rhi/Device/Semaphore.h"
#include "Rhi/Pipeline/ComputeKernel.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/Submission/EpochTracker.h"

#include <SDL3/SDL.h>
#include <chrono>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <thread>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    namespace {
        /// @brief The process's device, queue and instance, registered by the device context.
        WGPUDevice g_device = nullptr;
        WGPUQueue g_queue = nullptr;
        WGPUInstance g_instance = nullptr;
    } // namespace

    void RegisterWgpuDevice(WGPUDevice device, WGPUQueue queue, WGPUInstance instance) noexcept {
        g_device = device;
        g_queue = queue;
        g_instance = instance;
    }

    WGPUDevice RegisteredWgpuDevice() noexcept {
        return g_device;
    }

    WGPUQueue RegisteredWgpuQueue() noexcept {
        return g_queue;
    }

    WGPUInstance RegisteredWgpuInstance() noexcept {
        return g_instance;
    }

    /**
     * @brief The seam's device on a backend with one queue and no fences.
     *
     * What the standard interface gives is a device, a queue and completion callbacks.
     * What it does not give is a host-waitable fence or a timeline semaphore, and the
     * seam's submission contract leans on both. The honest state of this unit is
     * therefore: the device, the allocator, the caches and the epoch tracker are real,
     * and the submission primitives report what they cannot do rather than faking a
     * completion they cannot observe.
     *
     * There is no compute kernel cache here either, and the reason is a shape rather
     * than an omission: the seam's kernel is built from raw SPIR-V words, and this
     * backend receives WGSL through the bake. RequestComputeKernelFromModule is the
     * entry point that fits, and it needs a compute pipeline, which is the unit after
     * the buffers and textures.
     */
    struct DeviceContext::impl {
        /// Declared first: everything below reads the device through it.
        std::unique_ptr<DeviceInterface> device_interface{};
        std::unique_ptr<AllocatorState> allocator_state{};
        DeviceCapabilities capabilities{};
        std::unique_ptr<ImmutableResourceCache> immutable_resource_cache{};
        std::unique_ptr<DescriptorArena> descriptor_arena{};
        /// Declared last so that it is destroyed first: releasing the resources parked
        /// in it needs the allocator and the device to be alive.
        std::unique_ptr<EpochTracker> epoch_tracker{};
        /// There is no fence to key a completion on. The queue's own callback is what
        /// this backend will use once submissions exist; until then nothing is pending.
        std::size_t pending_completions{0};
        /// Submissions handed to the queue since the last completion this backend observed.
        /// A wait with nothing outstanding is already satisfied - the queue executes in order,
        /// so everything before it has finished.
        std::size_t submissions_since_completion{0};
        /// Compute kernels by module identity, owned here so a caller gets the same one back.
        std::map<std::string, std::unique_ptr<ComputeKernel>> compute_kernels{};
    };

    DeviceContext::DeviceContext(DeviceInterface::DeviceConfiguration cfg) : pimpl(std::make_unique<impl>()) {
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "webgpu: creating DeviceInterface");
        pimpl->device_interface = std::make_unique<DeviceInterface>(std::move(cfg));
        // The encoder records against the device without owning one, so the device and its
        // queue are registered here, where the process's only device is created.
        RegisterWgpuDevice(
            ToWgpuDevice(pimpl->device_interface->GetDevice()),
            wgpuDeviceGetQueue(ToWgpuDevice(pimpl->device_interface->GetDevice())),
            ToWgpuInstance(pimpl->device_interface->GetInstance())
        );
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "webgpu: DeviceInterface ready");
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "webgpu: creating cache");
        pimpl->immutable_resource_cache =
            std::make_unique<ImmutableResourceCache>(pimpl->device_interface->GetDevice());
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "webgpu: creating allocator");
        pimpl->allocator_state = std::make_unique<AllocatorState>(*pimpl->device_interface);
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "webgpu: creating tracker");
        pimpl->epoch_tracker = std::make_unique<EpochTracker>(pimpl->device_interface->GetDevice());
        // The arena reads the tracker and the cache, so both must exist first.
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "webgpu: creating arena");
        pimpl->descriptor_arena = std::make_unique<DescriptorArena>(*this);

        pimpl->allocator_state->SetRetireSink(pimpl->epoch_tracker.get());

        // The four the seam asks about, from the one limits block the device reports.
        // `wgpuDeviceGetLimits` takes an out-parameter and returns whether it filled it.
        WGPULimits limits{};
        if (!wgpuDeviceGetLimits(ToWgpuDevice(pimpl->device_interface->GetDevice()), &limits)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: the device did not report its limits.");
        }
        pimpl->capabilities = DeviceCapabilities{
            // Push constants have no equivalent: a kernel parameter block is a uniform
            // buffer here, so the seam sees zero rather than a lie.
            .max_push_constant_bytes = 0,
            .uniform_buffer_offset_alignment = limits.minUniformBufferOffsetAlignment,
            .max_storage_buffer_range = limits.maxStorageBufferBindingSize,
            .max_compute_invocations = limits.maxComputeInvocationsPerWorkgroup,
            .max_bind_groups = limits.maxBindGroups
        };
        SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "WebGPU device context ready.");
    }

    DeviceContext::~DeviceContext() = default;

    const DeviceCapabilities &DeviceContext::GetCapabilities() const noexcept {
        return pimpl->capabilities;
    }

    DeviceInterface &DeviceContext::GetDeviceInterface() noexcept {
        return *pimpl->device_interface;
    }
    const DeviceInterface &DeviceContext::GetDeviceInterface() const noexcept {
        return *pimpl->device_interface;
    }

    AllocatorState &DeviceContext::GetAllocatorState() noexcept {
        return *pimpl->allocator_state;
    }
    const AllocatorState &DeviceContext::GetAllocatorState() const noexcept {
        return *pimpl->allocator_state;
    }

    ImmutableResourceCache &DeviceContext::GetIRCache() noexcept {
        return *pimpl->immutable_resource_cache;
    }
    const ImmutableResourceCache &DeviceContext::GetIRCache() const noexcept {
        return *pimpl->immutable_resource_cache;
    }

    DescriptorArena &DeviceContext::GetDescriptorArena() noexcept {
        return *pimpl->descriptor_arena;
    }
    const DescriptorArena &DeviceContext::GetDescriptorArena() const noexcept {
        return *pimpl->descriptor_arena;
    }

    ComputeKernelCache &DeviceContext::GetComputeKernelCache() noexcept {
        // No kernel cache exists on this backend, and a reference to nothing is not a
        // useful error. This stops and says why rather than returning one.
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "WebGPU: there is no compute kernel cache on this backend. A kernel is built "
            "from WGSL through RequestComputeKernelFromModule once compute pipelines exist."
        );
        std::terminate();
    }

    const ComputeKernelCache &DeviceContext::GetComputeKernelCache() const noexcept {
        return const_cast<DeviceContext *>(this)->GetComputeKernelCache();
    }

    ComputeKernel &DeviceContext::RequestComputeKernel(
        std::string_view, const std::vector<std::uint32_t> &, std::string_view
    ) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "WebGPU: RequestComputeKernel takes SPIR-V words, which this backend cannot use. "
            "Use RequestComputeKernelFromModule."
        );
        std::terminate();
    }

    ComputeKernel &DeviceContext::RequestComputeKernelFromModule(
        std::string_view module_id,
        ShaderModuleHandle module,
        const ShaderInterfaceDescription &description,
        std::string_view entry_point,
        std::string_view debug_name
    ) {
        const std::string key{module_id};
        if (auto itr = pimpl->compute_kernels.find(key); itr != pimpl->compute_kernels.end()) {
            return *itr->second;
        }
        // `new` rather than `make_unique`: the constructor is private, and only the friend
        // context here can call it.
        auto kernel = std::unique_ptr<ComputeKernel>(new ComputeKernel(*this));
        // The description is what the bake wrote; the entry point this device was asked for
        // wins when the two name different functions.
        ShaderInterfaceDescription effective = description;
        if (!entry_point.empty()) {
            effective.entry_point = std::string{entry_point};
        }
        kernel->CreateFromModule(module, effective, debug_name);
        ComputeKernel &result = *pimpl->compute_kernels.emplace(key, std::move(kernel)).first->second;
        return result;
    }

    ComputeKernel *DeviceContext::FindComputeKernel(std::string_view module_id) noexcept {
        auto itr = pimpl->compute_kernels.find(std::string{module_id});
        return itr == pimpl->compute_kernels.end() ? nullptr : itr->second.get();
    }

    EpochTracker &DeviceContext::GetEpochTracker() noexcept {
        return *pimpl->epoch_tracker;
    }
    const EpochTracker &DeviceContext::GetEpochTracker() const noexcept {
        return *pimpl->epoch_tracker;
    }

    DeviceHandle DeviceContext::GetDevice() const noexcept {
        return pimpl->device_interface->GetDevice();
    }

    void DeviceContext::WaitForIdle() {
        // Process events until the queue has nothing in flight. This is the standard
        // interface's own pump: wgpuDevicePoll in wgpu.h is an extension a browser build
        // does not have.
        wgpuInstanceProcessEvents(ToWgpuInstance(pimpl->device_interface->GetInstance()));
        pimpl->epoch_tracker->ReleaseAllParked();
        pimpl->descriptor_arena->OnDeviceIdle();
    }

    void DeviceContext::PumpCompletions() {
        wgpuInstanceProcessEvents(ToWgpuInstance(pimpl->device_interface->GetInstance()));
    }

    void DeviceContext::UpdateDescriptorSets(std::span<const WriteDescriptorSet>) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "WebGPU: UpdateDescriptorSets has no meaning here. A bind group is created "
            "with its contents rather than written into afterwards; see "
            "DescriptorArena::Acquire."
        );
    }

    void DeviceContext::Submit(const Submission &submission, FenceHandle) {
        // The standard interface has one queue and no dependencies: a submission is an
        // ordered list of finished command buffers, and "wait for this semaphore" has no
        // meaning because nothing else can be in flight on a different queue.
        //
        // Host writes that no caller flushed are published here, before the queue sees the
        // commands that read them. That is the whole of the rule design D8 states: a caller writes
        // through the window, and the submission is what makes the write visible.
        pimpl->allocator_state->FlushPendingHostWrites();
        pimpl->submissions_since_completion++;
        std::vector<WGPUCommandBuffer> commands;
        commands.reserve(submission.command_buffers.size());
        for (const CommandBufferHandle &handle : submission.command_buffers) {
            WGPUCommandBuffer buffer = ToWgpuFinishedBuffer(handle);
            if (buffer == nullptr) {
                SDL_LogError(
                    SDL_LOG_CATEGORY_RENDER, "WebGPU: a submitted command buffer was never finished. Call End first."
                );
                continue;
            }
            commands.push_back(buffer);
        }
        if (commands.empty()) {
            return;
        }

        WGPUQueue queue = wgpuDeviceGetQueue(ToWgpuDevice(GetDevice()));
        wgpuQueueSubmit(queue, commands.size(), commands.data());

        // Completion is a callback rather than a fence. The submission carries it, so it is
        // registered here against the work just handed over.
        if (submission.on_complete) {
            auto *callback = new std::function<void()>(submission.on_complete);
            WGPUQueueWorkDoneCallbackInfo info{};
            info.mode = WGPUCallbackMode_AllowProcessEvents;
            info.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView, void *userdata1, void *) {
                auto *fn = static_cast<std::function<void()> *>(userdata1);
                if (status != WGPUQueueWorkDoneStatus_Success) {
                    SDL_LogError(
                        SDL_LOG_CATEGORY_RENDER,
                        "WebGPU: a submission completed with status %d.",
                        static_cast<int>(status)
                    );
                }
                if (fn != nullptr && *fn) {
                    (*fn)();
                }
                delete fn;
            };
            info.userdata1 = callback;
            wgpuQueueOnSubmittedWorkDone(queue, info);
        }
    }

    bool DeviceContext::WaitForFence(FenceHandle, uint64_t timeout) {
        // Completion arrives as a callback rather than through a fence, so waiting is: ask the
        // queue to call back when the work handed over so far is done, then dispatch events
        // until it does. `timeout == 0` is the poll form a caller uses to ask "can I reuse this
        // slot yet", and it dispatches once and answers.
        WGPUQueue queue = wgpuDeviceGetQueue(ToWgpuDevice(pimpl->device_interface->GetDevice()));
        if (queue == nullptr) {
            return true;
        }
        if (pimpl->submissions_since_completion == 0) {
            // Nothing has been handed to the queue since the last completion this backend
            // observed, and a queue executes in order, so everything before this point has
            // finished. Asking the device would be correct but is the one case where its
            // answer is never delivered: a work-done callback on an idle queue has been seen
            // not to fire, and every frame would begin by waiting for it.
            return true;
        }
        bool done = false;
        WGPUQueueWorkDoneCallbackInfo info{};
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void *userdata1, void *) {
            *static_cast<bool *>(userdata1) = true;
        };
        info.userdata1 = &done;
        wgpuQueueOnSubmittedWorkDone(queue, info);

        WGPUInstance instance = ToWgpuInstance(pimpl->device_interface->GetInstance());
        const auto begin = std::chrono::steady_clock::now();
        // A poll still dispatches the device's events for a moment: the callback that answers
        // this question is delivered by that dispatch, and one pass over the event queue often
        // returns before the queue has reported the work. The deadline keeps the call a poll -
        // two milliseconds is far below a frame - while making it an answer rather than a coin
        // flip.
        constexpr uint64_t k_poll_budget_ns = 2'000'000;
        while (!done) {
            if (instance != nullptr) {
                wgpuInstanceProcessEvents(instance);
            }
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
            const uint64_t budget = timeout == 0 ? k_poll_budget_ns : timeout;
            if (budget != std::numeric_limits<uint64_t>::max() && static_cast<uint64_t>(elapsed) > budget) {
                break;
            }
#if defined(__EMSCRIPTEN__)
            // The browser answers on the event loop the caller is occupying, so the wait has to
            // yield to it; without this the callback can never be delivered.
            emscripten_sleep(0);
#else
            std::this_thread::sleep_for(std::chrono::microseconds(50));
#endif
        }
        if (done) {
            pimpl->submissions_since_completion = 0;
        }
        return done;
    }

    void DeviceContext::SignalSemaphore(const TimelineSignal &) {
        // Reported once, not once per frame: the renderer signals a semaphore every frame, and a
        // line per frame buries every other diagnostic the page prints. The seam's single ordered
        // queue is what makes the missing signal harmless here, and a repeated message does not
        // make it more true.
        static bool reported = false;
        if (reported) {
            return;
        }
        reported = true;
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "WebGPU: SignalSemaphore is not implemented. The standard interface has no "
            "timeline semaphore."
        );
    }
} // namespace Engine::Rhi
