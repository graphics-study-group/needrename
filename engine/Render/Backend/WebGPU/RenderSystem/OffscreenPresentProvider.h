#ifndef RENDER_BACKEND_WEBGPU_OFFSCREENPRESENTPROVIDER_INCLUDED
#define RENDER_BACKEND_WEBGPU_OFFSCREENPRESENTPROVIDER_INCLUDED

#include <memory>

#include "Render/RenderSystem/IPresentProvider.h"

namespace Engine {
    class DeviceInterface;
    class AllocatorState;
    class RenderTargetTexture;

    namespace RenderSystemState {
        /**
         * @brief An offscreen presentation target on the WebGPU backend.
         *
         * There is no swapchain here, and nothing to present to until a surface provider exists.
         * What this provides is the rest of what a frame loop needs: an extent, a colour format, a
         * fixed image count, and an acquire that always succeeds. The final image lives in a render
         * target the caller owns, so there is no copy to prepare and no present to perform.
         *
         * This is enough for a headless frame loop and for a test that reads its result back. It is
         * not a browser surface - `wgpuSurfaceConfigure` and `wgpuSurfaceGetCurrentTexture` are
         * what a surface provider needs, and that is a separate unit.
         */
        class OffscreenPresentProvider : public IPresentProvider {
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            OffscreenPresentProvider(
                const Rhi::DeviceInterface &device_interface,
                const Rhi::AllocatorState &allocator,
                Rhi::Extent2D extent,
                Rhi::ImageFormat color_format,
                uint32_t image_count
            );
            ~OffscreenPresentProvider() override;

            Rhi::Extent2D GetExtent() const override;
            Rhi::ImageFormat GetColorFormat() const override;
            uint32_t GetImageCount() const override;

            /// @brief Nothing is presented, so nothing waits on a completion semaphore.
            bool NeedsPresentSemaphore() const noexcept override {
                return false;
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
