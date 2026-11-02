#ifndef RENDER_BACKEND_WEBGPU_SURFACEPRESENTPROVIDER_INCLUDED
#define RENDER_BACKEND_WEBGPU_SURFACEPRESENTPROVIDER_INCLUDED

#include <memory>

struct SDL_Window;

#include "Render/RenderSystem/IPresentProvider.h"

namespace Engine {
    class DeviceInterface;
    class AllocatorState;
    class RenderTargetTexture;

    namespace RenderSystemState {
        /**
         * @brief A presentation target built from the window the engine already created.
         *
         * The surface is made from the window's HWND on desktop and from the shell's canvas on the
         * web, because those are the two things a WebGPU surface can be made from. Images are not
         * a queue here: each frame acquires the surface's current texture, the copy pass draws the
         * final render target into it, and present hands it back.
         *
         * The copy is a blit through the seam's `ImageBlitDescription`, so this provider records
         * nothing itself - it supplies a command buffer for FrameManager to submit, exactly as the
         * Vulkan swapchain provider does.
         */
        class SurfacePresentProvider : public IPresentProvider {
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            SurfacePresentProvider(
                const Rhi::DeviceInterface &device_interface, SDL_Window *window, Rhi::Extent2D extent
            );
            ~SurfacePresentProvider() override;

            Rhi::Extent2D GetExtent() const override;
            Rhi::ImageFormat GetColorFormat() const override;
            uint32_t GetImageCount() const override;

            /// @brief Presenting waits on the frame's completion, so the semaphore is used.
            bool NeedsPresentSemaphore() const noexcept override {
                return true;
            }

            uint32_t AcquireNextImage(
                Rhi::DeviceHandle device, Rhi::SemaphoreHandle image_ready_semaphore, uint64_t timeout
            ) override;

            Rhi::CommandBufferHandle PrepareCopy(
                Rhi::DeviceHandle device,
                const RenderTargetTexture &final_rtt,
                uint32_t image_index,
                Rhi::MemoryAccessTypeImageBits last_access
            ) override;

            bool Present(Rhi::DeviceHandle device, uint32_t image_index, Rhi::SemaphoreHandle frame_done_semaphore)
                override;
            void Recreate(Rhi::Extent2D new_extent) override;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif

