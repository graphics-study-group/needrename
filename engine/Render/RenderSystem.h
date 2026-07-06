#ifndef RENDER_RENDERSYSTEM
#define RENDER_RENDERSYSTEM

#include <glm.hpp>
#include <memory>

#include "Render/Memory/MemoryAccessTypes.h"

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
    class CommandBuffer;
    class RenderTargetTexture;

    namespace ConstantData {
        struct PerCameraStruct;
    };

    namespace RenderSystemState {
        class DeviceInterface;
        class Swapchain;
        class AllocatorState;
        class FrameManager;
        class RendererManager;
        class ImmutableResourceCache;
        class CameraManager;
        class SceneDataManager;
        class ResizableRTTManager;

        class MaterialInstanceManager;
        class MaterialLibraryManager;
        class StaticMeshResourceManager;
    }; // namespace RenderSystemState

    /**
     * @brief Main locator for the rendering system services.
     */
    class RenderSystem : public std::enable_shared_from_this<RenderSystem> {
    private:
        class impl;
        std::unique_ptr<impl> pimpl;

        std::tuple<
            RenderSystemState::MaterialInstanceManager *,
            RenderSystemState::MaterialLibraryManager *,
            RenderSystemState::StaticMeshResourceManager *>
            m_resource_managers{};

    public:
        RenderSystem(std::weak_ptr<SDLWindow> parent_window);

        RenderSystem(const RenderSystem &) = delete;
        RenderSystem(RenderSystem &&) = delete;
        void operator=(const RenderSystem &) = delete;
        void operator=(RenderSystem &&) = delete;

        /**
         * @brief Create the render system and initialize all subsystems.
         *
         * @see RenderSystemState::DeviceInterface
         * for details on how the Vulkan abstraction layer is initialized.
         */
        void Create();

        ~RenderSystem();

        /// @brief Halt the execution of the current thread and wait for GPU to be idle.
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
         * Blits the present_texture to the swapchain image that is currently
         * allocated for the frame (via `FrameManager::GetFramebuffer()`).
         * This is the only time in a frame that the swapchain image is written
         * to.
         *
         * This method also does resource (i.e. swapchain) recreation if necessary.
         * If you end a frame by manually calling `FrameManager::CompleteFrame()`,
         * then you must make sure that these resources are recreated correctly yourself.
         */
        void CompleteFrame(
            const RenderTargetTexture &present_texture,
            MemoryAccessTypeImageBits last_access,
            uint32_t width,
            uint32_t height,
            uint32_t offset_x = 0,
            uint32_t offset_y = 0
        );

        /**
         * @brief Complete the rendering of the current frame.
         * Defaults last_access to color attachment write.
         */
        void CompleteFrame(
            const RenderTargetTexture &present_texture,
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

        /// @brief Get the allocator service
        const RenderSystemState::AllocatorState &GetAllocatorState() const;
        /// @brief Get the swapchain manager
        const RenderSystemState::Swapchain &GetSwapchain() const;
        /// @brief Get the frame manager
        RenderSystemState::FrameManager &GetFrameManager();
        /// @brief Get the renderer manager
        RenderSystemState::RendererManager &GetRendererManager();
        /// @brief Get the immutable resource cache
        RenderSystemState::ImmutableResourceCache &GetIRCache();
        /// @brief Get the camera manager
        RenderSystemState::CameraManager &GetCameraManager();
        /// @brief Get the manager for scene data (e.g lightings)
        RenderSystemState::SceneDataManager &GetSceneDataManager();
        /// @brief Get the manager for resizable render target textures
        RenderSystemState::ResizableRTTManager &GetResizableRTTManager();

        template <typename ResourceManagerType>
        ResourceManagerType &GetRenderResourceManager() {
            return *std::get<ResourceManagerType *>(m_resource_managers);
        }
    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // RENDER_RENDERSYSTEM
