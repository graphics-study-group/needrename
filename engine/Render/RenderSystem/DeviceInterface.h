#ifndef RENDER_RENDERSYSTEM_DEVICEINTERFACE_INCLUDED
#define RENDER_RENDERSYSTEM_DEVICEINTERFACE_INCLUDED

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

struct SDL_Window;

namespace vk {
    class Instance;
    class SurfaceKHR;
    class PhysicalDevice;
    class Device;
    namespace detail {
        class DispatchLoaderBase;
    }
} // namespace vk

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
            std::unique_ptr<impl> pimpl;

        public:
            /// @brief Configuration for creating the underlying Vulkan device.
            struct DeviceConfiguration {
                /// Parent window to create a surface from
                SDL_Window *window;
                /// Arbitrary application name. Does not affect Vulkan behavior.
                std::string application_name;
                /// Arbitrary application version. Does not affect Vulkan behavior.
                uint32_t application_version;
                /// Vulkan-Hpp dynamic dispatcher, can be null, in which case uses the default one.
                vk::detail::DispatchLoaderDynamic *dynamic_dispatcher;
            };

            /// @brief Types of integer limits that can be queried for.
            enum class PhysicalDeviceLimitInteger {
                MaxUniformBufferSize,
                MaxStorageBufferSize,
                UniformBufferOffsetAlignment,
                StorageBufferOffsetAlignment,
                AsyncTransferImageGranularityWidth,
                AsyncTransferImageGranularityHeight,
                AsyncTransferImageGranularityDepth
            };

            /// @brief Types of floating point limits that can be queried for.
            enum class PhysicalDeviceLimitFloat {
            };

            /// @brief Types of queue families.
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

            /**
             * @brief Construct the device interface by the configuration.
             *
             * It sets up Vulkan instance, surface, physical device and
             * logical device accordingly. Queues and command pools are also
             * created.
             */
            DeviceInterface(DeviceConfiguration cfg);
            ~DeviceInterface();

            /// @brief Get the current unique instance of Vulkan.
            vk::Instance GetInstance() const;
            /// @brief Get the current unique surface of the OS.
            vk::SurfaceKHR GetSurface() const;
            /// @brief Get the current selected physical device.
            vk::PhysicalDevice GetPhysicalDevice() const;
            /// @brief Get the current logical device.
            vk::Device GetDevice() const;
            /**
             * @brief Get the queue information of current logical device.
             *
             * @note Queue and Queue Families are two distinct notions in
             * Vulkan. They should not be confused.
             */
            const QueueInfo &GetQueueInfo() const;

            /**
             * @brief Query information on swapchain supports (e.g. surface formats).
             */
            SwapchainSupport GetSwapchainSupport() const;

            /**
             * @brief Query information on queue family indices of different type.
             *
             * Queries for graphics and graphics present queue family are guaranteed
             * to return a non-null optional.
             *
             * @note Queue and Queue Families are two distinct notions in
             * Vulkan. They should not be confused.
             */
            std::optional<uint32_t> GetQueueFamily(QueueFamilyType type) const noexcept;

            /**
             * @brief Query the limit of current selected physical device.
             */
            uint32_t QueryLimit(PhysicalDeviceLimitInteger limit) const;
            /// @overload uint32_t DeviceInterface::QueryLimit(PhysicalDeviceLimitInteger limit) const
            float QueryLimit(PhysicalDeviceLimitFloat limit) const;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_DEVICEINTERFACE_INCLUDED
