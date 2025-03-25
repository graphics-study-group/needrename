#include "LayoutTransferHelper.h"

namespace Engine {
    vk::ImageMemoryBarrier2 LayoutTransferHelper::GetAttachmentBarrier(AttachmentTransferType type, vk::Image image)
    {
        auto [scope1, scope2, layouts] = std::tuple{GetScope1(type), GetScope2(type), GetLayouts(type)};
        vk::ImageSubresourceRange subresource{
            GetAspectFlags(type),
            0, vk::RemainingMipLevels,
            0, vk::RemainingArrayLayers
        };
        vk::ImageMemoryBarrier2 barrier {
            scope1.first, scope1.second,
            scope2.first, scope2.second,
            layouts.first, layouts.second,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, image,
            subresource
        };
        return barrier;
    }
    vk::ImageMemoryBarrier2 LayoutTransferHelper::GetTextureBarrier(TextureTransferType type, vk::Image image)
    {
        auto [scope1, scope2, layouts] = std::tuple{GetScope1(type), GetScope2(type), GetLayouts(type)};
        vk::ImageSubresourceRange subresource{
            GetAspectFlags(type),
            0, vk::RemainingMipLevels,
            0, vk::RemainingArrayLayers
        };
        vk::ImageMemoryBarrier2 barrier {
            scope1.first, scope1.second,
            scope2.first, scope2.second,
            layouts.first, layouts.second,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, image,
            subresource
        };
        return barrier;
    }
    std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> LayoutTransferHelper::GetScope1(AttachmentTransferType type)
    {
        // Locate scope 1 at the last stage where the old layout is needed
        switch(type) {
        case AttachmentTransferType::ColorAttachmentPrepare:
            return std::make_pair(vk::PipelineStageFlagBits2::eBottomOfPipe, vk::AccessFlagBits2::eNone);
        case AttachmentTransferType::ColorAttachmentPresent:
            return std::make_pair(vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        case AttachmentTransferType::DepthAttachmentPrepare:
            return std::make_pair(vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        }
        __builtin_unreachable();
    }
    std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> LayoutTransferHelper::GetScope2(AttachmentTransferType type)
    {
        // Locate scope 2 at the first stage where the new layout is needed
        switch(type) {
        case AttachmentTransferType::ColorAttachmentPrepare:
            return std::make_pair(vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite);
        case AttachmentTransferType::ColorAttachmentPresent:
            return std::make_pair(vk::PipelineStageFlagBits2::eBottomOfPipe, vk::AccessFlagBits2::eNone);
        case AttachmentTransferType::DepthAttachmentPrepare:
            return std::make_pair(vk::PipelineStageFlagBits2::eEarlyFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        }
        __builtin_unreachable();
    }
    std::pair<vk::ImageLayout, vk::ImageLayout> LayoutTransferHelper::GetLayouts(AttachmentTransferType type)
    {
        switch(type) {
        case AttachmentTransferType::ColorAttachmentPrepare:
            return std::make_pair(vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
        case AttachmentTransferType::ColorAttachmentPresent:
            return std::make_pair(vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR);
        case AttachmentTransferType::DepthAttachmentPrepare:
            return std::make_pair(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthAttachmentOptimal);
        }
        __builtin_unreachable();
    }
    vk::ImageAspectFlags LayoutTransferHelper::GetAspectFlags(AttachmentTransferType type)
    {
        switch(type) {
        case AttachmentTransferType::ColorAttachmentPrepare:
            return vk::ImageAspectFlagBits::eColor;
        case AttachmentTransferType::ColorAttachmentPresent:
            return vk::ImageAspectFlagBits::eColor;
        case AttachmentTransferType::DepthAttachmentPrepare:
            return vk::ImageAspectFlagBits::eDepth;
        }
        __builtin_unreachable();
    }

    std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> LayoutTransferHelper::GetScope1(TextureTransferType type)
    {
        switch (type) {
        case TextureTransferType::TextureUploadBefore:
        case TextureTransferType::TextureClearBefore:
            return std::make_pair(vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);
        case TextureTransferType::TextureUploadAfter:
        case TextureTransferType::TextureClearAfter:
            return std::make_pair(vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        }
        __builtin_unreachable();
    }
    std::pair<vk::PipelineStageFlagBits2, vk::AccessFlags2> LayoutTransferHelper::GetScope2(TextureTransferType type)
    {
        switch (type) {
        case TextureTransferType::TextureUploadBefore:
        case TextureTransferType::TextureClearBefore:
            return std::make_pair(vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        case TextureTransferType::TextureUploadAfter:
        case TextureTransferType::TextureClearAfter:
            return std::make_pair(vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);
        }
        __builtin_unreachable();
    }
    std::pair<vk::ImageLayout, vk::ImageLayout> LayoutTransferHelper::GetLayouts(TextureTransferType type)
    {
        switch (type) {
        case TextureTransferType::TextureUploadBefore:
        case TextureTransferType::TextureClearBefore:
            return std::make_pair(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
        case TextureTransferType::TextureUploadAfter:
        case TextureTransferType::TextureClearAfter:
            return std::make_pair(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
        }
        __builtin_unreachable();
    }
    vk::ImageAspectFlags LayoutTransferHelper::GetAspectFlags(TextureTransferType type)
    {
        return vk::ImageAspectFlagBits::eColor;
    }
}
