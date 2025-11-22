#ifndef ENGINE_RENDER_RENDERSYSTEM_INCLUDED
#define ENGINE_RENDER_RENDERSYSTEM_INCLUDED

#include <glm.hpp>
#include <memory>

// Suppress warning from std::enable_shared_from_this
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace vk {
    class Instance;
    class SurfaceKHR;
    class Device;
    class PhysicalDevice;
} // namespace vk

namespace Engine {
    class SDLWindow;
    class RendererComponent;
    class Camera;
    class GraphicsCommandBuffer;

    namespace ConstantData {
        struct PerCameraStruct;
    };

    namespace RenderSystemState {
        class DeviceInterface;
        class Swapchain;
        class AllocatorState;
        class GlobalConstantDescriptorPool;
        class MaterialRegistry;
        class FrameManager;
        class RendererManager;
        class SwapchainSupport;
        class QueueFamilyIndices;
        class QueueInfo;
        class SamplerManager;
        class CameraManager;
    }; // namespace RenderSystemState

    class RenderSystem : public std::enable_shared_from_this<RenderSystem> {
    private:
        class impl;
        std::unique_ptr<impl> pimpl;

    public:
        using SwapchainSupport = Engine::RenderSystemState::SwapchainSupport;
        using QueueFamilyIndices = Engine::RenderSystemState::QueueFamilyIndices;
        using QueueInfo = Engine::RenderSystemState::QueueInfo;

        RenderSystem(std::weak_ptr<SDLWindow> parent_window);

        RenderSystem(const RenderSystem &) = delete;
        RenderSystem(RenderSystem &&) = delete;
        void operator=(const RenderSystem &) = delete;
        void operator=(RenderSystem &&) = delete;

        void Create();

        ~RenderSystem();

        void WaitForIdle() const;

        /// @brief Update the swapchain in response of a window resize etc.
        /// @note You need to recreate depth images and framebuffers that refer to the swap chain.
        void UpdateSwapchain();

        /**
         * @brief Start the rendering of the next frame.
         * 
         * This method also submits necessary data to GPU, meaning that all logic 
         * that might change these data must finish before calling it. 
         * If you start a frame by manually calling `FrameManager::StartFrame()`,
         * then you must make sure that these data are submitted correctly yourself.
         */
        uint32_t StartFrame();

        void CompleteFrame();

        /**
         * @brief Get a handle to the Vulkan logical device that the current Render
         * System runs on.
         */
        vk::Device GetDevice() const;

        /**
         * @brief Get interfaces to all unique Vulkan low-level objects managed by the
         * system.
         */
        const RenderSystemState::DeviceInterface &GetDeviceInterface() const;

        const RenderSystemState::AllocatorState &GetAllocatorState() const;

        const RenderSystemState::Swapchain &GetSwapchain() const;

        const RenderSystemState::GlobalConstantDescriptorPool &GetGlobalConstantDescriptorPool() const;

        RenderSystemState::MaterialRegistry &GetMaterialRegistry();

        RenderSystemState::FrameManager &GetFrameManager();

        RenderSystemState::RendererManager &GetRendererManager();

        RenderSystemState::SamplerManager & GetSamplerManager();

        RenderSystemState::CameraManager & GetCameraManager();

    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // ENGINE_RENDER_RENDERSYSTEM_INCLUDED
