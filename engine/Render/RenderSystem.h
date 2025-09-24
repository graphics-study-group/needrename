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

        void SetActiveCamera(std::weak_ptr<Camera>);
        std::weak_ptr<Camera> GetActiveCamera() const;
        uint32_t GetActiveCameraId() const;
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

        const RenderSystemState::AllocatorState &GetAllocatorState() const;

        const QueueFamilyIndices &GetQueueFamilies() const;

        const QueueInfo &getQueueInfo() const;

        const RenderSystemState::Swapchain &GetSwapchain() const;

        const RenderSystemState::GlobalConstantDescriptorPool &GetGlobalConstantDescriptorPool() const;

        RenderSystemState::MaterialRegistry &GetMaterialRegistry();

        RenderSystemState::FrameManager &GetFrameManager();

        RenderSystemState::RendererManager &GetRendererManager();

        RenderSystemState::SamplerManager & GetSamplerManager();

        void WritePerCameraConstants(const ConstantData::PerCameraStruct &data, uint32_t in_flight_index);
    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // ENGINE_RENDER_RENDERSYSTEM_INCLUDED
