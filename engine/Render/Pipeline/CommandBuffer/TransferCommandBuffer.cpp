#include "TransferCommandBuffer.h"
#include "Render/Memory/Texture.h"

namespace Engine {
    TransferCommandBuffer::TransferCommandBuffer(vk::CommandBuffer cb) : ICommandBuffer(cb) {
    }

    void TransferCommandBuffer::BlitColorImage(const Texture &src, const Texture &dst) {
        const auto &src_desc{src.GetTextureDescription()}, &dst_desc{dst.GetTextureDescription()};
        BlitColorImage(
            src,
            dst,
            TextureArea{
                .mip_level = 0,
                .array_layer_base = 0,
                .array_layer_count = src_desc.array_layers,
                .x0 = 0,
                .y0 = 0,
                .z0 = 0,
                .x1 = static_cast<int32_t>(src_desc.width),
                .y1 = static_cast<int32_t>(src_desc.height),
                .z1 = static_cast<int32_t>(src_desc.depth)
            },
            TextureArea{
                .mip_level = 0,
                .array_layer_base = 0,
                .array_layer_count = dst_desc.array_layers,
                .x0 = 0,
                .y0 = 0,
                .z0 = 0,
                .x1 = static_cast<int32_t>(dst_desc.width),
                .y1 = static_cast<int32_t>(dst_desc.height),
                .z1 = static_cast<int32_t>(dst_desc.depth)
            }
        );
    }

    void TransferCommandBuffer::BlitColorImage(
        const Texture &src, const Texture &dst, TextureArea src_area, TextureArea dst_area
    ) {
        assert(0 <= src_area.x0 && 0 <= src_area.y0 && 0 <= src_area.z0);
        assert(0 <= dst_area.x0 && 0 <= dst_area.y0 && 0 <= dst_area.z0);
        assert(src_area.x0 < src_area.x1 && src_area.y0 < src_area.y1 && src_area.z0 < src_area.z1);
        assert(dst_area.x0 < dst_area.x1 && dst_area.y0 < dst_area.y1 && dst_area.z0 < src_area.z1);

        vk::ImageBlit2 blit{
            vk::ImageSubresourceLayers{
                vk::ImageAspectFlagBits::eColor,
                src_area.mip_level,
                src_area.array_layer_base,
                src_area.array_layer_count
            },
            {vk::Offset3D{src_area.x0, src_area.y0, src_area.z0}, vk::Offset3D{src_area.x1, src_area.y1, src_area.z1}},
            vk::ImageSubresourceLayers{
                vk::ImageAspectFlagBits::eColor,
                dst_area.mip_level,
                dst_area.array_layer_base,
                dst_area.array_layer_count
            },
            {vk::Offset3D{dst_area.x0, dst_area.y0, dst_area.z0}, vk::Offset3D{dst_area.x1, dst_area.y1, dst_area.z1}},
        };
        vk::BlitImageInfo2 bii{
            src.GetImage(),
            vk::ImageLayout::eTransferSrcOptimal,
            dst.GetImage(),
            vk::ImageLayout::eTransferDstOptimal,
            {blit},
            vk::Filter::eLinear
        };
        cb.blitImage2(bii);
    }
} // namespace Engine
