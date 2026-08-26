#ifndef RENDER_RENDERSYSTEM_OFFSCREENPRESENTPROVIDER_INCLUDED
#define RENDER_RENDERSYSTEM_OFFSCREENPRESENTPROVIDER_INCLUDED

#include "IPresentProvider.h"
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
                vk::Extent2D extent,
                vk::Format color_format,
                uint32_t image_count
            );
            ~OffscreenPresentProvider() override;

            vk::Extent2D GetExtent() const override;
            vk::Format GetColorFormat() const override;
            uint32_t GetImageCount() const override;

            uint32_t AcquireNextImage(
                vk::Device device, vk::Semaphore image_ready_semaphore, uint64_t timeout
            ) override;

            vk::CommandBuffer PrepareCopy(
                vk::Device device,
                const RenderTargetTexture &final_rtt,
                uint32_t image_index,
                Rhi::MemoryAccessTypeImageBits last_access
            ) override;

            bool Present(vk::Device device, uint32_t image_index, vk::Semaphore frame_done_semaphore) override;

            void Recreate(vk::Extent2D new_extent) override;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif
