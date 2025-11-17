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
                SDL_Window * window;
                std::string application_name;
                uint32_t application_version;

                vk::detail::DispatchLoaderDynamic * dynamic_dispatcher{&VULKAN_HPP_DEFAULT_DISPATCHER};
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
        };
    }
}

#endif // RENDER_RENDERSYSTEM_DEVICEINTERFACE_INCLUDED
