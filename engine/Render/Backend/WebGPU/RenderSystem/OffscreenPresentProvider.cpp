#include "Render/Backend/WebGPU/RenderSystem/OffscreenPresentProvider.h"

#include <SDL3/SDL.h>

#include "Render/Resource/RenderTargetTexture.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceInterface.h"

namespace Engine::RenderSystemState {
    struct OffscreenPresentProvider::impl {
        Rhi::Extent2D extent{};
        Rhi::ImageFormat color_format{Rhi::ImageFormat::R8G8B8A8UNorm};
        uint32_t image_count{1};
    };

    OffscreenPresentProvider::OffscreenPresentProvider(
        const Rhi::DeviceInterface &device_interface,
        const Rhi::AllocatorState &allocator,
        Rhi::Extent2D extent,
        Rhi::ImageFormat color_format,
        uint32_t image_count
    ) :
        pimpl(std::make_unique<impl>()) {
        // The device and the allocator are not needed: the presentation target is a render
        // target the caller owns. Kept in the signature because the surface provider that
        // follows will need both.
        static_cast<void>(device_interface);
        static_cast<void>(allocator);
        pimpl->extent = extent;
        pimpl->color_format = color_format;
        pimpl->image_count = image_count == 0 ? 1 : image_count;
    }

    OffscreenPresentProvider::~OffscreenPresentProvider() = default;

    Rhi::Extent2D OffscreenPresentProvider::GetExtent() const {
        return pimpl->extent;
    }

    Rhi::ImageFormat OffscreenPresentProvider::GetColorFormat() const {
        return pimpl->color_format;
    }

    uint32_t OffscreenPresentProvider::GetImageCount() const {
        return pimpl->image_count;
    }

    uint32_t OffscreenPresentProvider::AcquireNextImage(Rhi::DeviceHandle, Rhi::SemaphoreHandle, uint64_t) {
        // Nothing to acquire: the frame renders into the caller's target and there is no queue of
        // images to rotate through. Image zero is always available.
        return 0;
    }

    Rhi::CommandBufferHandle OffscreenPresentProvider::PrepareCopy(
        Rhi::DeviceHandle, const RenderTargetTexture &, uint32_t, Rhi::MemoryAccessTypeImageBits
    ) {
        // No copy: the final render target is what the caller reads.
        return {};
    }

    bool OffscreenPresentProvider::Present(Rhi::DeviceHandle, uint32_t, Rhi::SemaphoreHandle) {
        // Nothing is presented, and nothing is ever out of date.
        return false;
    }

    void OffscreenPresentProvider::Recreate(Rhi::Extent2D new_extent) {
        pimpl->extent = new_extent;
    }
} // namespace Engine::RenderSystemState
