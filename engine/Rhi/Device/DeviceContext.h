#ifndef ENGINE_RHI_DEVICECONTEXT_INCLUDED
#define ENGINE_RHI_DEVICECONTEXT_INCLUDED

#include "Rhi/Device/DeviceInterface.h"

#include <cstdint>
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
        std::unique_ptr<DeviceInterface> m_device_interface;
        std::unique_ptr<AllocatorState> m_allocator_state;
        std::unique_ptr<ImmutableResourceCache> m_immutable_resource_cache;
        /// @note Declared after the device and the resource cache because its
        /// pools hold device objects, and before the tracker it reads so that
        /// the ledger is torn down first.
        std::unique_ptr<DescriptorArena> m_descriptor_arena;
        /// @note Declared after the resource cache and the arena because a
        /// kernel owns a pipeline and a pipeline layout, and names a
        /// descriptor-set layout the arena resolved and the resource cache
        /// owns; declaring it here destroys the kernels *before* them.
        std::unique_ptr<ComputeKernelCache> m_compute_kernel_cache;
        /// @note Declared last so that it is destroyed *first*: releasing the
        /// resources still parked in the tracker needs the allocator and the
        /// device to be alive.
        std::unique_ptr<EpochTracker> m_epoch_tracker;

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

        /// @brief Get the underlying Vulkan device.
        vk::Device GetDevice() const noexcept;

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
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_DEVICECONTEXT_INCLUDED
