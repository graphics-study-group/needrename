#ifndef ENGINE_RENDER_RENDERSYSTEM_INCLUDED
#define ENGINE_RENDER_RENDERSYSTEM_INCLUDED

#include <memory>
#include <vulkan/vulkan.hpp>
#include <glm.hpp>

#include "Render/RenderSystem/Structs.h"

// Suppress warning from std::enable_shared_from_this
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace Engine
{
    class SDLWindow;
    class RendererComponent;
    class CameraComponent;
    class GraphicsCommandBuffer;

    namespace ConstantData {
        struct PerCameraStruct;
    };

    namespace RenderSystemState {
        class Swapchain;
        class AllocatorState;
        class GlobalConstantDescriptorPool;
        class MaterialRegistry;
        class FrameManager;
    };

    class RenderSystem : public std::enable_shared_from_this<RenderSystem>
    {
    private:
        class impl;
        std::unique_ptr <impl> pimpl;

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

        /**
         * @brief Draw meshes using the matrices of the active camera (identity matrices if default).
         */
        void DrawMeshes(uint32_t pass = 0);

        /**
         * @brief Draw meshes with the given view and projection matrices.
         * 
         * This method writes camera uniforms and viewport state.
         * Then it binds materials and records draw calls on the current render command buffer.
         */
        void DrawMeshes(const glm::mat4 & view_matrix, const glm::mat4 & projection_matrix, vk::Extent2D extent, uint32_t pass = 0);
        
        void RegisterComponent(std::shared_ptr <RendererComponent>);
        void ClearComponent();

        void SetActiveCamera(std::shared_ptr <CameraComponent>);
        void WaitForIdle() const;

        /// @brief Update the swapchain in response of a window resize etc.
        /// @note You need to recreate depth images and framebuffers that refer to the swap chain.
        void UpdateSwapchain();

        uint32_t StartFrame();

        void CompleteFrame();

        vk::Instance getInstance() const;

        vk::SurfaceKHR getSurface() const;

        vk::Device getDevice() const;

        vk::PhysicalDevice GetPhysicalDevice() const;

        const RenderSystemState::AllocatorState & GetAllocatorState() const;

        const QueueFamilyIndices & GetQueueFamilies() const;

        const QueueInfo & getQueueInfo () const;

        const RenderSystemState::Swapchain & GetSwapchain() const;

        const RenderSystemState::GlobalConstantDescriptorPool & GetGlobalConstantDescriptorPool() const;

        RenderSystemState::MaterialRegistry & GetMaterialRegistry();

        RenderSystemState::FrameManager & GetFrameManager ();

        void WritePerCameraConstants(const ConstantData::PerCameraStruct & data, uint32_t in_flight_index);
    };
}

#pragma GCC diagnostic pop

#endif // ENGINE_RENDER_RENDERSYSTEM_INCLUDED
