#ifndef RENDER_RENDERSYSTEM
#define RENDER_RENDERSYSTEM

#include "Render/render_export.h"
#include <glm.hpp>
#include <memory>
#include <tuple>

#include "Rhi/Device/MemoryAccessTypes.h"

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
    namespace Rhi {
        class AllocatorState;
        class DeviceContext;
        class DeviceInterface;
        class ImmutableResourceCache;
    } // namespace Rhi
    class SDLWindow;
    class RendererComponent;
    class Camera;
    class CommandBuffer;
    class RenderTargetTexture;
    class IPresentProvider;

    namespace ConstantData {
        struct PerCameraStruct;
    };

    namespace RenderSystemState {
        class FrameManager;
        class RendererManager;
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
    class RENDER_API RenderSystem : public std::enable_shared_from_this<RenderSystem> {
    private:
        class impl;
        std::unique_ptr<impl> pimpl;

        std::tuple<
            RenderSystemState::MaterialInstanceManager *,
            RenderSystemState::MaterialLibraryManager *,
            RenderSystemState::StaticMeshResourceManager *>
            m_resource_managers{};

    public:
        RenderSystem(std::weak_ptr<SDLWindow> parent_window, Rhi::DeviceContext &device_context);

        RenderSystem(const RenderSystem &) = delete;
        RenderSystem(RenderSystem &&) = delete;
        void operator=(const RenderSystem &) = delete;
        void operator=(RenderSystem &&) = delete;

        /**
         * @brief Create the render system and initialize all subsystems.
         *
         * @see Rhi::DeviceInterface
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
         * Ends the main command buffer, records the copy command buffer via
         * `IPresentProvider::PrepareCopy`, submits ONE batch containing the
         * main render CB and the copy CB, then presents (see
         * `FrameManager::SubmitFrame`). This is the only time in a frame that
         * the swapchain image is written to.
         *
         * This method also does resource (i.e. swapchain) recreation if necessary.
         *
         * @param present_texture Final render target to present.
         * @param last_access Access mode of `present_texture` in its last pass
         *                    (used to derive the copy source barrier).
         */
        void CompleteFrame(const RenderTargetTexture &present_texture, Rhi::MemoryAccessTypeImageBits last_access);

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
        const Rhi::DeviceInterface &GetDeviceInterface() const;

        /// @brief Get the allocator service
        const Rhi::AllocatorState &GetAllocatorState() const;
        /// @brief Get the device-scoped GPU facilities (device, allocator, resource cache)
        Rhi::DeviceContext &GetDeviceContext();
        /// @brief Get the present provider (windowed or headless)
        IPresentProvider &GetPresentProvider();
        /// @brief Get the frame manager
        RenderSystemState::FrameManager &GetFrameManager();
        /// @brief Get the renderer manager
        RenderSystemState::RendererManager &GetRendererManager();
        /// @brief Get the immutable resource cache
        Rhi::ImmutableResourceCache &GetIRCache();
        /// @brief Get the camera manager
        RenderSystemState::CameraManager &GetCameraManager();
        /// @brief Get the manager for scene data (e.g lightings)
        RenderSystemState::SceneDataManager &GetSceneDataManager();
        /// @brief Get the manager for resizable render target textures
        RenderSystemState::ResizableRTTManager &GetResizableRTTManager();

        template <typename ResourceManagerType>
        ResourceManagerType *GetRenderResourceManager() {
            return std::get<ResourceManagerType *>(m_resource_managers);
        }
    };
} // namespace Engine

#pragma GCC diagnostic pop

#endif // RENDER_RENDERSYSTEM
