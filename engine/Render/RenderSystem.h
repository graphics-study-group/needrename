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
    class RenderTargetTexture;

    enum class MemoryAccessTypeImageBits;

    namespace ConstantData {
        struct PerCameraStruct;
    };

    namespace RenderSystemState {
        class DeviceInterface;
        class Swapchain;
        class AllocatorState;
        class MaterialRegistry;
        class FrameManager;
        class RendererManager;
        class SamplerManager;
        class CameraManager;
        class SceneDataManager;
    }; // namespace RenderSystemState

    class RenderSystem : public std::enable_shared_from_this<RenderSystem> {
    private:
        class impl;
        std::unique_ptr<impl> pimpl;

    public:

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
         * 
         * @todo buffer and texture submissions are completed by `FrameManager`.
         * Maybe we should unify these two data streams.
         */
        uint32_t StartFrame();

        /**
         * @brief Complete the rendering of the current frame.
         * 
         * This method also does resource (i.e. swapchain) recreation if necessary.
         * If you end a frame by manually calling `FrameManager::CompleteFrame()`,
         * then you must make sure that these resources are recreated correctly yourself.
         */
        void CompleteFrame(
            const RenderTargetTexture & present_texture,
            MemoryAccessTypeImageBits last_access,
            uint32_t width,
            uint32_t height,
            uint32_t offset_x = 0,
            uint32_t offset_y = 0
        );
        void CompleteFrame(
            const RenderTargetTexture & present_texture,
            uint32_t width,
            uint32_t height,
            uint32_t offset_x = 0,
            uint32_t offset_y = 0
        );

        /**
         * @brief Get a handle to the Vulkan logical device that the current Render
         * System runs on.
         * 
         * Shorthand for `GetDeviceInterface().GetDevice()`
         */
        vk::Device GetDevice() const;

        /**
         * @brief Get interfaces to all unique Vulkan low-level objects managed by the
         * system.
         */
        const RenderSystemState::DeviceInterface &GetDeviceInterface() const;

        const RenderSystemState::AllocatorState &GetAllocatorState() const;

        const RenderSystemState::Swapchain &GetSwapchain() const;

        RenderSystemState::MaterialRegistry &GetMaterialRegistry();

        RenderSystemState::FrameManager &GetFrameManager();

        RenderSystemState::RendererManager &GetRendererManager();

        RenderSystemState::SamplerManager & GetSamplerManager();

        RenderSystemState::CameraManager & GetCameraManager();

        RenderSystemState::SceneDataManager & GetSceneDataManager();

    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // ENGINE_RENDER_RENDERSYSTEM_INCLUDED
