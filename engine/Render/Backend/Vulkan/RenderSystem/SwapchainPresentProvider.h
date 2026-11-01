#ifndef RENDER_RENDERSYSTEM_SWAPCHAINPRESENTPROVIDER_INCLUDED
#define RENDER_RENDERSYSTEM_SWAPCHAINPRESENTPROVIDER_INCLUDED

#include "Render/RenderSystem/IPresentProvider.h"
#include "Render/render_export.h"
#include <memory>
#include <vector>

namespace Engine {
    namespace Rhi {
        class DeviceInterface;
    }
    namespace RenderSystemState {

        class RENDER_API SwapchainPresentProvider : public IPresentProvider {
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            SwapchainPresentProvider();
            ~SwapchainPresentProvider() override;

            void Initialize(const Rhi::DeviceInterface &device_interface, Rhi::Extent2D expected_extent);

            Rhi::Extent2D GetExtent() const override;
            Rhi::ImageFormat GetColorFormat() const override;
            uint32_t GetImageCount() const override;

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





