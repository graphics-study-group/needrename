#ifndef RENDER_RENDERSYSTEM_DEVICEINTERFACE_INCLUDED
#define RENDER_RENDERSYSTEM_DEVICEINTERFACE_INCLUDED

#include <memory>
#include <string>
#include <cstdint>

struct SDL_Window;

namespace vk {
    class Instance;
    class SurfaceKHR;
    class PhysicalDevice;
    class Device;
    namespace detail {
        class DispatchLoaderBase;
    }
}

namespace Engine {
    namespace RenderSystemState {
        struct QueueFamilyIndices;
        struct SwapchainSupport;

        /**
         * @brief A monolithic class for manage Vulkan enabled hardware.
         * Including instance, surface, physical device and device management.
         * 
         * All returned handles from this class can be assumed to be invariant
         * across a session, meaning that you are free to cache these results.
         */
        class DeviceInterface {
            struct impl;
            std::unique_ptr <impl> pimpl;

        public:

            struct DeviceConfiguration {
                // Parent window to create a surface from
                SDL_Window * window;
                // Arbitrary application name. Does not affect Vulkan behavior.
                std::string application_name;
                // Arbitrary application version. Does not affect Vulkan behavior.
                uint32_t application_version;
                // Vulkan-Hpp dynamic dispatcher, can be null, in which case uses the default one.
                vk::detail::DispatchLoaderDynamic * dynamic_dispatcher;
            };

            enum class PhysicalDeviceLimitInteger {
                MaxUniformBufferSize,
                MaxStorageBufferSize,
                UniformBufferOffsetAlignment,
                StorageBufferOffsetAlignment
            };

            enum class PhysicalDeviceLimitFloat {

            };

            DeviceInterface(DeviceConfiguration cfg);
            ~DeviceInterface();

            vk::Instance GetInstance() const;
            vk::SurfaceKHR GetSurface() const;
            vk::PhysicalDevice GetPhysicalDevice() const;
            vk::Device GetDevice() const;
    
            const QueueFamilyIndices & GetQueueFamilies() const;
            const SwapchainSupport & GetSwapchainSupport() const;
            const QueueInfo & GetQueueInfo() const;

            uint32_t QueryLimit(PhysicalDeviceLimitInteger limit) const;
            float QueryLimit(PhysicalDeviceLimitFloat limit) const;
        };
    }
}

#endif // RENDER_RENDERSYSTEM_DEVICEINTERFACE_INCLUDED
