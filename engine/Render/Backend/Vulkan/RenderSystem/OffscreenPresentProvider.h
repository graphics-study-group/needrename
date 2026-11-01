#ifndef RENDER_RENDERSYSTEM_OFFSCREENPRESENTPROVIDER_INCLUDED
#define RENDER_RENDERSYSTEM_OFFSCREENPRESENTPROVIDER_INCLUDED

#include "Render/RenderSystem/IPresentProvider.h"
#include "Render/render_export.h"
#include <memory>

namespace Engine {
    namespace Rhi {
        class DeviceInterface;
        class AllocatorState;
    } // namespace Rhi
    namespace RenderSystemState {

        /**
         * @brief Offscreen present provider: real CPU-visible present targets.
         *
         * For headless/offscreen rendering. Lazily allocates one host-visible
         * `ReadbackFromDevice` buffer per frame-in-flight (matching
         * `GetImageCount()`) on the first `PrepareCopy` call, then records a
         * real `copyImageToBuffer` into the target for the given image index.
         * `Present` is a no-op returning `false`. A readback-callback API is
         * intentionally out of scope.
         */
        class RENDER_API OffscreenPresentProvider : public IPresentProvider {
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

            /// @brief Headless present is a no-op, so it never waits on the
            /// frame completion semaphore. Signaling it would leave it
            /// permanently signaled and make the next frame's signal invalid.
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

            bool Present(Rhi::DeviceHandle device, uint32_t image_index, Rhi::SemaphoreHandle frame_done_semaphore) override;

            void Recreate(Rhi::Extent2D new_extent) override;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif






