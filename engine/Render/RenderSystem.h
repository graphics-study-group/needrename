#ifndef RENDER_RENDERSYSTEM_INCLUDED
#define RENDER_RENDERSYSTEM_INCLUDED

#include "Functional/SDLWindow.h"
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>

#include "Render/Pipeline/CommandBuffer.h"
#include "Render/RenderSystem/Synch/InflightTwoStageSynch.h"

#include "Render/RenderSystem/Instance.h"
#include "Render/RenderSystem/PhysicalDevice.h"
#include "Render/RenderSystem/Swapchain.h"

namespace Engine
{
    class RendererComponent;
    class CameraComponent;

    class RenderSystem
    {
    public:

        using SwapchainSupport = Engine::RenderSystemState::SwapchainSupport;
        using QueueFamilyIndices = Engine::RenderSystemState::QueueFamilyIndices;

        struct QueueInfo {
            vk::Queue graphicsQueue;
            vk::UniqueCommandPool graphicsPool;
            vk::Queue presentQueue;
            vk::UniqueCommandPool presentPool;
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

        /// @brief Blocks and waits for a fence, signaling a frame is ready for rendering.
        /// Resets corresponding command buffer and fence.
        /// @param frame_index 
        /// @param timeout 
        void WaitForFrameBegin(uint32_t frame_index, uint64_t timeout = std::numeric_limits<uint64_t>::max());

        vk::Instance getInstance() const;
        vk::SurfaceKHR getSurface() const;
        vk::Device getDevice() const;
        const QueueInfo & getQueueInfo () const;
        const RenderSystemState::Swapchain & GetSwapchain() const;
        const Synchronization & getSynchronization() const;
        RenderCommandBuffer & GetGraphicsCommandBuffer(uint32_t frame_index);

        uint32_t GetNextImage(uint32_t in_flight_index, uint64_t timeout = std::numeric_limits<uint64_t>::max());
        vk::Result Present(uint32_t frame_index, uint32_t in_flight_index);
        
    protected:
        /// @brief Create a vk::SurfaceKHR.
        /// It should be called right after instance creation, before selecting a physical device.
        void CreateSurface();

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

        RenderSystemState::PhysicalDevice m_selected_physical_device {};
        // Order of declaration effects destructing order!

        RenderSystemState::Instance m_instance {};
        vk::UniqueSurfaceKHR m_surface{};
        vk::UniqueDevice m_device{};
        
        QueueInfo  m_queues {};
        RenderSystemState::Swapchain m_swapchain{};

        std::unique_ptr <Synchronization> m_synch {};
        std::vector <RenderCommandBuffer> m_commandbuffers {};
    };
}

#endif // RENDER_RENDERSYSTEM_INCLUDED
