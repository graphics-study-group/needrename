#include "TransferCommandBuffer.h"
#include "Render/Memory/Texture.h"
#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"

namespace Engine {
    TransferCommandBuffer::TransferCommandBuffer(RenderSystem &system, vk::CommandBuffer cb) :
        ICommandBuffer(cb), m_system(system) {
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
