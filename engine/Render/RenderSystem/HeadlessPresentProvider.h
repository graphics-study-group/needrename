#ifndef RENDER_RENDERSYSTEM_HEADLESSPRESENTPROVIDER_INCLUDED
#define RENDER_RENDERSYSTEM_HEADLESSPRESENTPROVIDER_INCLUDED

#include "IPresentProvider.h"
#include <memory>

namespace Engine {
    namespace Rhi {
        class DeviceInterface;
    }
    namespace RenderSystemState {

        class HeadlessPresentProvider : public IPresentProvider {
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            HeadlessPresentProvider(
                const Rhi::DeviceInterface &device_interface,
                vk::Extent2D extent,
                vk::Format color_format,
                uint32_t image_count
            );
            ~HeadlessPresentProvider() override;

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
