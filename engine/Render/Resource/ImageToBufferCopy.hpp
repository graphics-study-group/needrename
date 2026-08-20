#ifndef RENDER_RESOURCE_IMAGETOBUFFERCOPY_INCLUDED
#define RENDER_RESOURCE_IMAGETOBUFFERCOPY_INCLUDED

#include "Render/Resource/MemoryAccessHelper.hpp"
#include "Render/Resource/RenderTargetTexture.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/MemoryAccessTypes.h"

#include <vulkan/vulkan.hpp>

namespace Engine {
    /**
     * @brief Record a CPU-readable copy of an image into a device buffer.
     *
     * Pure, stateless recorder: whoever needs the readback calls it; unused
     * means zero cost. Records a pre-barrier that transitions the image from
     * `GetImageLayout(last_access)` to `eTransferSrcOptimal`, a
     * `copyImageToBuffer` of mip 0 at the texture's full extent (tight-packed
     * rows, row-major, top-to-bottom, RGBA8 for R8G8B8A8 targets), then a
     * post-barrier that restores the image to `GetImageLayout(last_access)` so
     * downstream consumers keep their layout contract (e.g. the swapchain blit
     * re-deriving the source layout).
     *
     * The destination buffer is expected to be host-visible/coherent
     * (`ReadbackFromDevice`); its contents are CPU-visible once the enclosing
     * batch's fence is signaled.
     *
     * @param cb          Command buffer to record into.
     * @param image       Source image to copy from.
     * @param dst         Destination buffer (host-visible readback target).
     * @param last_access Access mode the source image was last used with, used
     *                    to derive the pre/post layout and access barriers.
     */
    inline void RecordCopyImageToBuffer(
        vk::CommandBuffer cb,
        const RenderTargetTexture &image,
        const Rhi::DeviceBuffer &dst,
        Rhi::MemoryAccessTypeImageBits last_access
    ) {
        const auto &desc = image.GetTextureDescription();

        // Pre-barrier: source image into a copyable layout.
        vk::ImageMemoryBarrier2 pre_barrier{
            vk::PipelineStageFlagBits2::eAllCommands,
            GetAccessFlags({last_access}),
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferRead,
            GetImageLayout({last_access}),
            vk::ImageLayout::eTransferSrcOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            image.GetImage(),
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, pre_barrier});

        vk::BufferImageCopy region{
            0, // bufferOffset
            0, // bufferRowLength (tight packing)
            0, // bufferImageHeight
            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            vk::Offset3D{0, 0, 0},
            vk::Extent3D{desc.width, desc.height, desc.depth}
        };
        cb.copyImageToBuffer(image.GetImage(), vk::ImageLayout::eTransferSrcOptimal, dst.GetBuffer(), region);

        // Post-barrier: restore the source image to its nominal layout so the
        // frame-completion blit (or any later pass) still observes it.
        vk::ImageMemoryBarrier2 post_barrier{
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferRead,
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryRead,
            vk::ImageLayout::eTransferSrcOptimal,
            GetImageLayout({last_access}),
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            image.GetImage(),
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, post_barrier});
    }
} // namespace Engine

#endif // RENDER_RESOURCE_IMAGETOBUFFERCOPY_INCLUDED
