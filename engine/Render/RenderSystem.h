#ifndef RENDER_RENDERSYSTEM_INCLUDED
#define RENDER_RENDERSYSTEM_INCLUDED

#include "Functional/SDLWindow.h"
#include <set>
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>

namespace Engine
{
    class RendererComponent;
    class CameraComponent;

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

    class RenderSystem
    {
    public:

        RenderSystem(std::weak_ptr <SDLWindow> parent_window);

        RenderSystem(const RenderSystem &) = delete;
        RenderSystem(RenderSystem &&) = delete;
        void operator= (const RenderSystem &) = delete;
        void operator= (RenderSystem &&) = delete; 

        ~RenderSystem();

        void Render();
        void RegisterComponent(std::shared_ptr <RendererComponent>);
        void SetActiveCamera(std::shared_ptr <CameraComponent>);
        
        
    protected:

        /// @brief Create a Vulkan instance.
        /// @param appInfo vk::ApplicationInfo
        void CreateInstance(const vk::ApplicationInfo & appInfo);

        /// @brief Check if validation layer VK_LAYER_KHRONOS_validation is available.
        /// @return true if available
        bool CheckValidationLayer();

        /// @brief Create a vk::SurfaceKHR.
        /// It should be called right after instance creation, before selecting a physical device.
        void CreateSurface();

        /// @brief Select a suitable physical device.
        /// Should be called after surface creation, before logical device creation.
        /// @return vk::PhysicalDevice
        vk::PhysicalDevice SelectPhysicalDevice() const;
        
        /// @brief Check if a device is suitable
        /// @param device vk::PhysicalDevice
        /// @return true if it is.
        bool IsDeviceSuitable(const vk::PhysicalDevice & device) const;

        /// @brief Fill up a queue family struct of a given physical device.
        /// @param device vk::PhysicalDevice
        /// @return QueueFamilyIndices
        QueueFamilyIndices FillQueueFamily(const vk::PhysicalDevice & device) const;
        
        /// @brief 
        /// @param device 
        /// @return 
        SwapchainSupport FillSwapchainSupport(const vk::PhysicalDevice & device) const;

        /// @brief 
        /// @param support 
        /// @return 
        std::tuple <vk::Extent2D, vk::SurfaceFormatKHR, vk::PresentModeKHR>
        SelectSwapchainConfig(const SwapchainSupport & support) const;

        /// @brief Create a logical device from selected physical device.
        /// @param selectedPhysicalDevice vk::PhysicalDevice
        void CreateLogicalDevice(const vk::PhysicalDevice & selectedPhysicalDevice);

        /// @brief 
        /// @param selectedPhysicalDevice 
        /// @param indices 
        void CreateSwapchain(const vk::PhysicalDevice & selectedPhysicalDevice);

        static constexpr std::string_view validation_layer_name = "VK_LAYER_KHRONOS_validation";
        static constexpr std::array <std::string_view, 1> device_extension_name = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};


        std::weak_ptr <SDLWindow> m_window;
        // TODO: data: mesh, texture, light
        std::vector <std::shared_ptr<RendererComponent>> m_components {};
        std::shared_ptr <CameraComponent> m_active_camera {};

        // Order of declaration effects destructing order!

        vk::UniqueInstance m_instance{};
        vk::UniqueSurfaceKHR m_surface{};
        vk::UniqueDevice m_device{};
        
        struct {
            vk::Queue graphicsQueue;
            vk::Queue presentQueue;
        } m_queues {};

        struct {
            vk::UniqueSwapchainKHR swapchain;
            // Images retreived from swapchain don't require clean up.
            std::vector <vk::Image> images;
            // However, image views do need clean up.
            std::vector <vk::UniqueImageView> imageViews;
            vk::SurfaceFormatKHR format;
            vk::Extent2D extent;
        } m_swapchain{};
    };
}

#endif // RENDER_RENDERSYSTEM_INCLUDED
