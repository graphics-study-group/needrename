#ifndef RENDER_RENDERSYSTEM_PHYSICALDEVICE_INCLUDED
#define RENDER_RENDERSYSTEM_PHYSICALDEVICE_INCLUDED

#include "Render/VkWrapper.tcc"

#include <array>

namespace Engine::RenderSystemState {

    class PhysicalDevice {
    public:

        struct QueueFamilyIndices
        {
            std::optional <uint32_t> graphics {};
            std::optional <uint32_t> present {};

            bool isComplete() {
                if (!graphics.has_value()) return false;
                if (!present.has_value()) return false;
                return true;
            }
        };

        struct SwapchainSupport
        {
            vk::SurfaceCapabilitiesKHR capabilities;
            // std::unordered_set requires default constructor
            std::vector <vk::SurfaceFormatKHR> formats;
            std::vector <vk::PresentModeKHR> modes;
        };

        /// @brief Select a suitable physical device.
        /// Should be called after surface creation, before logical device creation.
        /// @return vk::PhysicalDevice
        static PhysicalDevice SelectPhysicalDevice(vk::Instance instance, vk::SurfaceKHR surface);

        vk::PhysicalDevice get() const;
        const SwapchainSupport & GetSwapchainSupport () const;
        const QueueFamilyIndices & GetQueueFamilyIndices () const;
        const vk::PhysicalDeviceMemoryProperties & GetMemoryProperties () const;

    protected:
        
        /// @brief Check if a device is suitable
        /// @param device vk::PhysicalDevice
        /// @param surface vk::SurfaceKHR
        /// @return true if it is.
        static bool IsDeviceSuitable (vk::PhysicalDevice device, vk::SurfaceKHR surface);
        static QueueFamilyIndices FillQueueFamilyIndices (vk::PhysicalDevice device, vk::SurfaceKHR surface);
        static SwapchainSupport FillSwapchainSupport (vk::PhysicalDevice device, vk::SurfaceKHR surface);

        static constexpr std::array <const char *, 1> DEVICE_EXTENSION_NAMES = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        SwapchainSupport m_support {};
        QueueFamilyIndices m_indices {};
        vk::PhysicalDeviceMemoryProperties m_memory_properties {};
        vk::PhysicalDevice m_device {};
    };
}

#endif // RENDER_RENDERSYSTEM_PHYSICALDEVICE_INCLUDED
