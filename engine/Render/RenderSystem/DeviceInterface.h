#ifndef RENDER_RENDERSYSTEM_DEVICEINTERFACE_INCLUDED
#define RENDER_RENDERSYSTEM_DEVICEINTERFACE_INCLUDED

#include <memory>
#include <string>
#include <cstdint>
#include <optional>

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
        struct QueueInfo;
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
                StorageBufferOffsetAlignment,
                AsyncTransferImageGranularityWidth,
                AsyncTransferImageGranularityHeight,
                AsyncTransferImageGranularityDepth
            };

            enum class PhysicalDeviceLimitFloat {

            };

            enum class QueueFamilyType {
                // Main graphics queue family that supports all operations.
                GraphicsMain,
                // Whether present from graphics queue family is supported.
                // For modern hardwares it can be assumed that graphics main
                // queue family always supports present.
                GraphicsPresent,
                // Optional asychronous compute queue family.
                AsynchronousCompute,
                // Whether present from asynchronous compute queue family is supported.
                // If this value exists, it is guranteed to be the same as
                // `AsynchronousCompute` enum value.
                AsynchronousComputePresent,
                // Optional asychronous transfer (DMA) queue family.
                AsynchronousTransfer,
            };

            DeviceInterface(DeviceConfiguration cfg);
            ~DeviceInterface();

            vk::Instance GetInstance() const;
            vk::SurfaceKHR GetSurface() const;
            vk::PhysicalDevice GetPhysicalDevice() const;
            vk::Device GetDevice() const;

            const QueueInfo & GetQueueInfo() const;

            /**
             * @brief Query information on swapchain supports (e.g. surface formats).
             */
            SwapchainSupport GetSwapchainSupport() const;

            /**
             * @brief Query information on queue family indices of different type.
             * 
             * Queries for graphics and graphics present queue family are guaranteed
             * to return non-null optional.
             */
            std::optional<uint32_t> GetQueueFamily(QueueFamilyType type) const noexcept;


            uint32_t QueryLimit(PhysicalDeviceLimitInteger limit) const;
            float QueryLimit(PhysicalDeviceLimitFloat limit) const;
        };
    }
}

#endif // RENDER_RENDERSYSTEM_DEVICEINTERFACE_INCLUDED
