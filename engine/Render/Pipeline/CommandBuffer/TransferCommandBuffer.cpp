#include "TransferCommandBuffer.h"
#include "Render/Memory/Texture.h"
#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"

namespace Engine {
    TransferCommandBuffer::TransferCommandBuffer(RenderSystem &system, vk::CommandBuffer cb) :
        ICommandBuffer(cb), m_system(system) {
    }

    void TransferCommandBuffer::BlitColorImage(const Texture &src, const Texture &dst) {
        const auto &src_desc{src.GetTextureDescription()}, &dst_desc{dst.GetTextureDescription()};
        BlitColorImage(src, dst, 
            TextureArea{
                .mip_level = 0,
                .array_layer_base = 0,
                .array_layer_count = src_desc.array_layers,
                .x0 = 0, .y0 = 0, .z0 = 0,
                .x1 = static_cast<int32_t>(src_desc.width), 
                .y1 = static_cast<int32_t>(src_desc.height), 
                .z1 = static_cast<int32_t>(src_desc.depth)
            }, 
            TextureArea{
                .mip_level = 0,
                .array_layer_base = 0,
                .array_layer_count = dst_desc.array_layers,
                .x0 = 0, .y0 = 0, .z0 = 0,
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

        vk::ImageBlit2 blit {
            vk::ImageSubresourceLayers{
                vk::ImageAspectFlagBits::eColor,
                src_area.mip_level,
                src_area.array_layer_base,
                src_area.array_layer_count
            },
            {
                vk::Offset3D{src_area.x0, src_area.y0, src_area.z0},
                vk::Offset3D{src_area.x1, src_area.y1, src_area.z1}
            },
            vk::ImageSubresourceLayers{
                vk::ImageAspectFlagBits::eColor,
                dst_area.mip_level,
                dst_area.array_layer_base,
                dst_area.array_layer_count
            },
            {
                vk::Offset3D{dst_area.x0, dst_area.y0, dst_area.z0},
                vk::Offset3D{dst_area.x1, dst_area.y1, dst_area.z1}
            },
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

    void TransferCommandBuffer::GenerateMipmaps(const Texture &img, AccessHelper::ImageAccessType previousAccess) {
        const auto &desc = img.GetTextureDescription();
        auto [width, height, total_levels] = std::make_tuple(desc.width, desc.height, desc.mipmap_levels);
        assert(
            desc.dimensions == 2 && desc.array_layers == 1 && "Mipmap is only supported for 2D textures with one layer."
        );

        auto previousAccessScope = AccessHelper::GetAccessScope(previousAccess);
        std::array<vk::ImageMemoryBarrier2, 2> barriers = {
            vk::ImageMemoryBarrier2{
                // This is a very conservative barrier.
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
                vk::PipelineStageFlagBits2::eBlit,
                vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                img.GetImage(),
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 1, 1, 0, 1}
            },
            vk::ImageMemoryBarrier2{
                std::get<0>(previousAccessScope),
                std::get<1>(previousAccessScope),
                vk::PipelineStageFlagBits2::eBlit,
                vk::AccessFlagBits2::eTransferRead,
                std::get<2>(previousAccessScope),
                vk::ImageLayout::eTransferSrcOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                img.GetImage(),
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
            }
        };
        // We still insert a barrier as if there is a transfer read.
        if (total_levels <= 1) {
            // We still insert a barrier as if there is a transfer read.
            cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, {}, {}, {barriers[1]}});
            return;
        }
        vk::DependencyInfo dep{};
        dep.setImageMemoryBarriers(barriers);
        cb.pipelineBarrier2(dep);

        barriers[1].srcStageMask = vk::PipelineStageFlagBits2::eBlit;
        barriers[1].srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barriers[1].oldLayout = vk::ImageLayout::eTransferDstOptimal;

        for (uint32_t level = 1; level < total_levels; level++) {
            cb.blitImage(
                img.GetImage(),
                vk::ImageLayout::eTransferSrcOptimal,
                img.GetImage(),
                vk::ImageLayout::eTransferDstOptimal,
                {vk::ImageBlit{
                    vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, level - 1, 0, 1},
                    {vk::Offset3D{0, 0, 0}, vk::Offset3D{width, height, 1}},
                    vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, level, 0, 1},
                    {vk::Offset3D{0, 0, 0}, vk::Offset3D{width >> 1, height >> 1, 1}}
                }},
                vk::Filter::eLinear
            );

            barriers[0].subresourceRange =
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, level + 1, 1, 0, 1};
            barriers[1].subresourceRange = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, level, 1, 0, 1};

            if (level == total_levels - 1) {
                dep.setImageMemoryBarriers({barriers[1]});
            } else {
                dep.setImageMemoryBarriers(barriers);
            }
            cb.pipelineBarrier2(dep);

            width >>= 1, height >>= 1;
        };
    }

} // namespace Engine
