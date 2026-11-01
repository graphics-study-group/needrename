#ifndef ENGINE_RHI_DEVICECONTEXT_INCLUDED
#define ENGINE_RHI_DEVICECONTEXT_INCLUDED

#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Pipeline/PipelineBuilder.h"

#include <cstdint>
#include <span>
#include <memory>
#include <string_view>
#include <vector>

namespace Engine::Rhi {
    class AllocatorState;
    class ComputeKernel;
    class ComputeKernelCache;
    class ImmutableResourceCache;
    class DescriptorArena;
    class EpochTracker;

    /**
     * @brief Aggregates the device-scoped GPU facilities.
     *
     * Owns the Vulkan device, the memory allocator, the immutable resource
     * cache, the descriptor arena and the GPU resource retirement facility for
     * exactly one device lifetime. Created once by the engine coordinator
     * (MainClass) and shared by RenderSystem and PhysicsSystem.
     *
     * @note Does NOT own active objects such as SubmissionHelper (physics and
     * render keep independent upload queues) or presentation-layer objects
     * (PresentProvider / FrameManager).
     *
     * @note Movable but non-copyable.
     */
    class RHI_API DeviceContext {
        /**
         * @brief What this backend hangs off the seam's device.
         *
         * Declared here and defined in the backend's own translation unit, so that
         * this header names no backend object - and so that a member the backend
         * needs, such as a pending completion keyed on a fence, has somewhere to live
         * without appearing in the interface.
         *
         * The destruction order inside it is load-bearing: the tracker is declared
         * last so that it goes first, because releasing the resources parked in it
         * needs the allocator and the device to still be alive.
         */
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        /**
         * @brief Create the device from the given configuration, initialize
         * the dynamic dispatch loader, and build the allocator, resource
         * cache, descriptor arena and retirement facility on top of it.
         */
        explicit DeviceContext(DeviceInterface::DeviceConfiguration cfg);
        ~DeviceContext();

        DeviceContext(const DeviceContext &) = delete;
        DeviceContext &operator=(const DeviceContext &) = delete;

        /// @brief What this device can do.
        const DeviceCapabilities &GetCapabilities() const noexcept;

        DeviceInterface &GetDeviceInterface() noexcept;
        const DeviceInterface &GetDeviceInterface() const noexcept;

        AllocatorState &GetAllocatorState() noexcept;
        const AllocatorState &GetAllocatorState() const noexcept;

        ImmutableResourceCache &GetIRCache() noexcept;
        const ImmutableResourceCache &GetIRCache() const noexcept;

        /**
         * @brief Get the device-scoped descriptor arena.
         *
         * The arena is the single owner of the engine's descriptor pools and
         * the only source of the engine's descriptor sets. It is available
         * without a render system, so a headless program can acquire sets.
         */
        DescriptorArena &GetDescriptorArena() noexcept;
        const DescriptorArena &GetDescriptorArena() const noexcept;

        /**
         * @brief Get the device-scoped compute kernel cache.
         *
         * Exactly one compute kernel exists per module identity on this device,
         * regardless of how many components dispatch it.
         */
        ComputeKernelCache &GetComputeKernelCache() noexcept;
        const ComputeKernelCache &GetComputeKernelCache() const noexcept;

        /**
         * @brief Request the compute kernel for a module identity, creating it lazily.
         *
         * The returned kernel is owned by the device context and stays valid for
         * its lifetime. Requesting an identity again returns the same kernel
         * without reading or scanning the module's words.
         *
         * @param module_id Identity of the module, unique per module on this device.
         * @param spirv_code The module's SPIR-V words; used only on the first request.
         * @param debug_name Debug name applied to the kernel and its objects.
         */
        ComputeKernel &RequestComputeKernel(
            std::string_view module_id, const std::vector<uint32_t> &spirv_code, std::string_view debug_name = {}
        );

        /**
         * @brief Request a compute kernel from already-compiled code.
         *
         * The overload above takes SPIR-V words, which is what a backend that reflects
         * at runtime needs. A backend that receives WGSL - or whatever form its own
         * bake produced - has no use for the words: it needs the module and the entry
         * point, which is what this takes.
         *
         * A backend implements the one it can and reports the other as unsupported.
         * Neither is a stub that silently returns something.
         *
         * @param module_id Identity of the module, unique per module on this device.
         * @param module The compiled code.
         * @param entry_point The function to dispatch.
         * @param debug_name Debug name applied to the kernel and its objects.
         */
        ComputeKernel &RequestComputeKernelFromModule(
            std::string_view module_id,
            ShaderModuleHandle module,
            std::string_view entry_point,
            std::string_view debug_name = {}
        );

        /**
         * @brief Find the compute kernel created for a module identity.
         *
         * @return The kernel, or nullptr when none exists for that identity.
         * Nothing is created and no module is read.
         */
        ComputeKernel *FindComputeKernel(std::string_view module_id) noexcept;

        /**
         * @brief Get the device-scoped GPU resource retirement facility.
         *
         * The tracker is installed as the allocator's retire sink, so every
         * buffer allocated through this context's allocator is retire-safe.
         */
        EpochTracker &GetEpochTracker() noexcept;
        const EpochTracker &GetEpochTracker() const noexcept;

        /// @brief Get the underlying device.
        DeviceHandle GetDevice() const noexcept;

        /**
         * @brief Wait for the device to be idle and broadcast that fact.
         *
         * This is the engine's single device-idle broadcast: it waits, releases
         * the retirement facility's parked allocations, and tells the arena
         * that everything issued so far is complete — which makes entries
         * eligible without releasing any of them. The wait is performed here so
         * that "notify without waiting" cannot be written.
         */
        void WaitForIdle();

        /**
         * @brief Point descriptor bindings at resources.
         *
         * A descriptor set is a table of bindings; filling it is what makes a
         * shader's declared inputs resolve to the buffers and images it will
         * actually read. Writes that name the same set are applied together, so a
         * frame that rewrites a set every frame is one call rather than a dozen.
         *
         * @param writes The bindings to point somewhere, in any order.
         */
        void UpdateDescriptorSets(std::span<const WriteDescriptorSet> writes);

        /**
         * @brief Hand one command buffer to a queue.
         *
         * The submission says what to wait for before it starts and what to
         * signal when it finishes; the fence, if given, is signalled when the
         * submission completes on the host side, which is what a caller waits on
         * to reuse the resources it read from.
         *
         * @param submission The command buffer and its dependencies.
         * @param fence Signalled when the submission completes, or empty.
         */
        void Submit(const Submission &submission, FenceHandle fence = {});

        /**
         * @brief Wait until a fence is signalled.
         *
         * @param fence The fence to wait on.
         * @param timeout_ns How long to wait, or the maximum value for no limit.
         * @return Whether the fence was signalled before the timeout.
         */
        bool WaitForFence(FenceHandle fence, uint64_t timeout_ns);

        /**
         * @brief Run the completion callbacks whose submissions have finished.
         *
         * A backend with completion callbacks runs them itself. This one observes
         * completion through fences, so it calls this from every place that already
         * looks at one - which is why a caller never has to schedule it.
         */
        void PumpCompletions();

        /**
         * @brief Signal a timeline semaphore from the host.
         *
         * A frame loop that has not submitted anything yet still has to give the
         * first wait something to observe; signalling is how it does that without
         * recording a submission.
         *
         * @param signal The semaphore and the value to move it to.
         */
        void SignalSemaphore(const TimelineSignal &signal);
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_DEVICECONTEXT_INCLUDED










