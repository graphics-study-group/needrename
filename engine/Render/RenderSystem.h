#ifndef RENDER_RENDERSYSTEM_INCLUDED
#define RENDER_RENDERSYSTEM_INCLUDED

#include "Functional/SDLWindow.h"
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>

#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/Synchronization.h"

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

        struct QueueInfo {
            vk::Queue graphicsQueue;
            vk::UniqueCommandPool graphicsPool;
            vk::Queue presentQueue;
            vk::UniqueCommandPool presentPool;
        };

        struct SwapchainInfo {
            vk::UniqueSwapchainKHR swapchain;
            // Images retreived from swapchain don't require clean up.
            std::vector <vk::Image> images;
            // However, image views do need clean up.
            std::vector <vk::UniqueImageView> imageViews;
            vk::SurfaceFormatKHR format;
            vk::Extent2D extent;
        };

        RenderSystem(std::weak_ptr <SDLWindow> parent_window);

        RenderSystem(const RenderSystem &) = delete;
        RenderSystem(RenderSystem &&) = delete;
        void operator= (const RenderSystem &) = delete;
        void operator= (RenderSystem &&) = delete; 

        ~RenderSystem();

        void Render();
        void RegisterComponent(std::shared_ptr <RendererComponent>);
        void SetActiveCamera(std::shared_ptr <CameraComponent>);
        void WaitForIdle() const;

        /// @brief Update the swapchain in response of a window resize etc.
        /// @note You need to recreate framebuffers that refer to the swap chain.
        void UpdateSwapchain();

        /// @brief Find a physical memory satisfying demands.
        /// @param type type of the requested memory
        /// @param properties properties of the request memory
        /// @return 
        uint32_t FindPhysicalMemory(uint32_t type, vk::MemoryPropertyFlags properties);

        vk::Instance getInstance() const;
        vk::SurfaceKHR getSurface() const;
        vk::Device getDevice() const;
        const QueueInfo & getQueueInfo () const;
        const SwapchainInfo & getSwapchainInfo() const;
        const Synchronization & getSynchronization() const;
        RenderCommandBuffer & GetGraphicsCommandBuffer(uint32_t frame_index);
        RenderCommandBuffer & GetGraphicsCommandBufferWaitAndReset(uint32_t frame_index, uint64_t timeout);

        uint32_t GetNextImage(uint32_t in_flight_index, uint64_t timeout);
        vk::Result Present(uint32_t frame_index, uint32_t in_flight_index);
        
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
        
        /// @brief Fill up swap chain support information
        /// @param device 
        /// @return SwapchainSupport
        SwapchainSupport FillSwapchainSupport(const vk::PhysicalDevice & device) const;

        /// @brief Select a swap chain config from all supported ones
        /// @param support 
        /// @return std::tuple <vk::Extent2D, vk::SurfaceFormatKHR, vk::PresentModeKHR>
        std::tuple <vk::Extent2D, vk::SurfaceFormatKHR, vk::PresentModeKHR>
        SelectSwapchainConfig(const SwapchainSupport & support) const;

        /// @brief Create a logical device from selected physical device.
        void CreateLogicalDevice();

        /// @brief Create a swap chain, possibly replace the older one.
        void CreateSwapchain();

        void CreateCommandPools(const QueueFamilyIndices & indices);

        static constexpr const char * validation_layer_name = "VK_LAYER_KHRONOS_validation";
        static constexpr std::array <std::string_view, 1> device_extension_name = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};


        std::weak_ptr <SDLWindow> m_window;
        // TODO: data: mesh, texture, light
        std::vector <std::shared_ptr<RendererComponent>> m_components {};
        std::shared_ptr <CameraComponent> m_active_camera {};

        vk::PhysicalDevice m_selected_physical_device {};
        vk::PhysicalDeviceMemoryProperties m_memory_properties {};
        // Order of declaration effects destructing order!

        vk::UniqueInstance m_instance{};
        vk::UniqueSurfaceKHR m_surface{};
        vk::UniqueDevice m_device{};
        
        QueueInfo  m_queues {};
        SwapchainInfo m_swapchain{};

        std::unique_ptr <Synchronization> m_synch {};
        std::vector <RenderCommandBuffer> m_commandbuffers {};
    };
}

#endif // RENDER_RENDERSYSTEM_INCLUDED
