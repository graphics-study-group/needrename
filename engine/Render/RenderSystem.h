#ifndef RENDER_RENDERSYSTEM_INCLUDED
#define RENDER_RENDERSYSTEM_INCLUDED

#include "Functional/SDLWindow.h"
#include <vector>
#include <memory>
#include <vulkan/vulkan.hpp>

#include "Render/Pipeline/CommandBuffer.h"

#include "Render/RenderSystem/AllocatorState.h"
#include "Render/RenderSystem/Instance.h"
#include "Render/RenderSystem/PhysicalDevice.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/MaterialDescriptorManager.h"
#include "Render/RenderSystem/MaterialRegistry.h"
#include "Render/RenderSystem/FrameManager.h"

// Suppress warning from std::enable_shared_from_this
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine
{
    class RendererComponent;
    class CameraComponent;

    namespace ConstantData {
        struct PerCameraStruct;
    }

    class RenderSystem : public std::enable_shared_from_this<RenderSystem>
    {
    public:

        using SwapchainSupport = Engine::RenderSystemState::SwapchainSupport;
        using QueueFamilyIndices = Engine::RenderSystemState::QueueFamilyIndices;

        struct QueueInfo {
            vk::Queue graphicsQueue;
            vk::UniqueCommandPool graphicsPool;
            vk::UniqueCommandPool graphicsOneTimePool;
            vk::Queue presentQueue;
            vk::UniqueCommandPool presentPool;
        };

        RenderSystem(std::weak_ptr <SDLWindow> parent_window);

        RenderSystem(const RenderSystem &) = delete;
        RenderSystem(RenderSystem &&) = delete;
        void operator= (const RenderSystem &) = delete;
        void operator= (RenderSystem &&) = delete; 

        void Create();

        ~RenderSystem();

        void DrawMeshes(uint32_t pass = 0);
        
        void RegisterComponent(std::shared_ptr <RendererComponent>);
        void ClearComponent();

        void SetActiveCamera(std::shared_ptr <CameraComponent>);
        void WaitForIdle() const;

        /// @brief Update the swapchain in response of a window resize etc.
        /// @note You need to recreate depth images and framebuffers that refer to the swap chain.
        void UpdateSwapchain();

        uint32_t StartFrame();

        void Render();

        void CompleteFrame();

        vk::Instance getInstance() const;

        vk::SurfaceKHR getSurface() const;

        vk::Device getDevice() const;

        vk::PhysicalDevice GetPhysicalDevice() const;

        const RenderSystemState::AllocatorState & GetAllocatorState() const;

        const QueueInfo & getQueueInfo () const;

        const RenderSystemState::Swapchain & GetSwapchain() const;

        // const Synchronization & getSynchronization() const;
        [[deprecated]]
        RenderCommandBuffer & GetGraphicsCommandBuffer(uint32_t frame_index);

        RenderCommandBuffer & GetCurrentCommandBuffer();

        const RenderSystemState::GlobalConstantDescriptorPool & GetGlobalConstantDescriptorPool() const;

        RenderSystemState::MaterialRegistry & GetMaterialRegistry();

        RenderSystemState::FrameManager & GetFrameManager ();

        void EnableDepthTesting();

        void WritePerCameraConstants(const ConstantData::PerCameraStruct & data, uint32_t in_flight_index);
        
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

        // uint32_t m_in_flight_frame_id = 0;

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
        RenderSystemState::AllocatorState m_allocator_state;
        RenderSystemState::Swapchain m_swapchain{};
        RenderSystemState::FrameManager m_frame_manager;
        RenderSystemState::GlobalConstantDescriptorPool m_descriptor_pool{};
        RenderSystemState::MaterialRegistry m_material_registry {};
    };
}

#pragma GCC diagnostic pop

#endif // RENDER_RENDERSYSTEM_INCLUDED
