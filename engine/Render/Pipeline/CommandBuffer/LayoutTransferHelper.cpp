#include "LayoutTransferHelper.h"

namespace Engine {

    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope1(LayoutTransferHelper::AttachmentBarrierType type)
    {
        using enum LayoutTransferHelper::AttachmentBarrierType;
        switch(type) {
            case ColorAttachmentRAW:
                return std::make_pair(vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
            case ColorAttachmentWAR:
                return std::make_pair(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead);
            case DepthAttachmentRAW:
                return std::make_pair(
                    vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests}, 
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                );
            case DepthAttachmentWAR:
                return std::make_pair(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead);
        }
        __builtin_unreachable();
    }
    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope2(LayoutTransferHelper::AttachmentBarrierType type)
    {
        using enum LayoutTransferHelper::AttachmentBarrierType;
        switch(type) {
            case ColorAttachmentRAW:
                return std::make_pair(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead);
            case ColorAttachmentWAR:
                return std::make_pair(vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
            case DepthAttachmentRAW:
                return std::make_pair(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead);
            case DepthAttachmentWAR:
                return std::make_pair(
                    vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests}, 
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                );
        }
        __builtin_unreachable();
    }
    std::pair<vk::ImageLayout, vk::ImageLayout> GetLayouts(LayoutTransferHelper::AttachmentBarrierType type)
    {
        using enum LayoutTransferHelper::AttachmentBarrierType;
        switch(type) {
            case ColorAttachmentRAW:
                return std::make_pair(vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eReadOnlyOptimal);
            case ColorAttachmentWAR:
                return std::make_pair(vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
            case DepthAttachmentRAW:
                return std::make_pair(vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eReadOnlyOptimal);
            case DepthAttachmentWAR:
                return std::make_pair(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        }
        __builtin_unreachable();
    }
    vk::ImageAspectFlags GetAspectFlags(LayoutTransferHelper::AttachmentBarrierType type)
    {
        using enum LayoutTransferHelper::AttachmentBarrierType;
        switch(type) {
            case ColorAttachmentRAW:
            case ColorAttachmentWAR:
                return vk::ImageAspectFlagBits::eColor;
            case DepthAttachmentRAW:
            case DepthAttachmentWAR:
                return vk::ImageAspectFlagBits::eDepth;
        }
        __builtin_unreachable();
    }
    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope1(LayoutTransferHelper::AttachmentTransferType type)
    {
        using enum LayoutTransferHelper::AttachmentTransferType;
        // Locate scope 1 at the last stage where the old layout is needed
        switch(type) {
        case ColorAttachmentPrepare:
            return std::make_pair(vk::PipelineStageFlagBits2::eBottomOfPipe, vk::AccessFlagBits2::eNone);
        case ColorAttachmentPresent:
            return std::make_pair(vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        case DepthAttachmentPrepare:
            return std::make_pair(vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        }
        __builtin_unreachable();
    }
    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope2(LayoutTransferHelper::AttachmentTransferType type)
    {
        using enum LayoutTransferHelper::AttachmentTransferType;
        // Locate scope 2 at the first stage where the new layout is needed
        switch(type) {
        case ColorAttachmentPrepare:
            return std::make_pair(vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite);
        case ColorAttachmentPresent:
            return std::make_pair(vk::PipelineStageFlagBits2::eBottomOfPipe, vk::AccessFlagBits2::eNone);
        case DepthAttachmentPrepare:
            return std::make_pair(vk::PipelineStageFlagBits2::eEarlyFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        }
        __builtin_unreachable();
    }
    std::pair<vk::ImageLayout, vk::ImageLayout> GetLayouts(LayoutTransferHelper::AttachmentTransferType type)
    {
        using enum LayoutTransferHelper::AttachmentTransferType;
        switch(type) {
        case ColorAttachmentPrepare:
            return std::make_pair(vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);
        case ColorAttachmentPresent:
            return std::make_pair(vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR);
        case DepthAttachmentPrepare:
            return std::make_pair(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        }
        __builtin_unreachable();
    }
    vk::ImageAspectFlags GetAspectFlags(LayoutTransferHelper::AttachmentTransferType type)
    {
        using enum LayoutTransferHelper::AttachmentTransferType;
        switch(type) {
        case ColorAttachmentPrepare:
            return vk::ImageAspectFlagBits::eColor;
        case ColorAttachmentPresent:
            return vk::ImageAspectFlagBits::eColor;
        case DepthAttachmentPrepare:
            return vk::ImageAspectFlagBits::eDepth;
        }
        __builtin_unreachable();
    }

    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope1(LayoutTransferHelper::TextureTransferType type)
    {
        using enum LayoutTransferHelper::TextureTransferType;
        switch (type) {
        case TextureUploadBefore:
        case TextureClearBefore:
            return std::make_pair(vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);
        case TextureUploadAfter:
        case TextureClearAfter:
            return std::make_pair(vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        }
        __builtin_unreachable();
    }
    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope2(LayoutTransferHelper::TextureTransferType type)
    {
        using enum LayoutTransferHelper::TextureTransferType;
        switch (type) {
        case TextureUploadBefore:
        case TextureClearBefore:
            return std::make_pair(vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        case TextureUploadAfter:
        case TextureClearAfter:
            return std::make_pair(vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);
        }
        __builtin_unreachable();
    }
    std::pair<vk::ImageLayout, vk::ImageLayout> GetLayouts(LayoutTransferHelper::TextureTransferType type)
    {
        using enum LayoutTransferHelper::TextureTransferType;
        switch (type) {
        case TextureUploadBefore:
        case TextureClearBefore:
            return std::make_pair(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
        case TextureUploadAfter:
        case TextureClearAfter:
            return std::make_pair(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eReadOnlyOptimal);
        }
        __builtin_unreachable();
    }
    vk::ImageAspectFlags GetAspectFlags(LayoutTransferHelper::TextureTransferType type)
    {
        return vk::ImageAspectFlagBits::eColor;
    }

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
    vk::ImageMemoryBarrier2 LayoutTransferHelper::GetAttachmentBarrier(AttachmentBarrierType type, vk::Image image)
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
}
