#ifndef ENGINE_RHI_DEVICECONTEXT_INCLUDED
#define ENGINE_RHI_DEVICECONTEXT_INCLUDED

#include "Rhi/DeviceInterface.h"

#include <memory>

namespace Engine::Rhi {
    class AllocatorState;
    class ImmutableResourceCache;

    /**
     * @brief Aggregates the device-scoped GPU facilities.
     *
     * Owns the Vulkan device, the memory allocator and the immutable resource
     * cache for exactly one device lifetime. Created once by the engine
     * coordinator (MainClass) and shared by RenderSystem and PhysicsSystem.
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

    public:
        /**
         * @brief Create the device from the given configuration, initialize
         * the dynamic dispatch loader, and build the allocator and resource
         * cache on top of it.
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

        /// @brief Get the underlying Vulkan device.
        vk::Device GetDevice() const noexcept;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_DEVICECONTEXT_INCLUDED
