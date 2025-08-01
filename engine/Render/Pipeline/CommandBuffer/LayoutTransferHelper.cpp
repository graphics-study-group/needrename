#include "LayoutTransferHelper.h"

#include <vulkan/vulkan.hpp>

namespace Engine {

    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope1(LayoutTransferHelper::TextureTransferType type) {
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
    std::pair<vk::PipelineStageFlags2, vk::AccessFlags2> GetScope2(LayoutTransferHelper::TextureTransferType type) {
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
    std::pair<vk::ImageLayout, vk::ImageLayout> GetLayouts(LayoutTransferHelper::TextureTransferType type) {
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
    vk::ImageAspectFlags GetAspectFlags(LayoutTransferHelper::TextureTransferType type) {
        return vk::ImageAspectFlagBits::eColor;
    }

    vk::ImageMemoryBarrier2 LayoutTransferHelper::GetTextureBarrier(TextureTransferType type, vk::Image image) {
        auto [scope1, scope2, layouts] = std::tuple{GetScope1(type), GetScope2(type), GetLayouts(type)};
        vk::ImageSubresourceRange subresource{
            GetAspectFlags(type), 0, vk::RemainingMipLevels, 0, vk::RemainingArrayLayers
        };
        vk::ImageMemoryBarrier2 barrier{
            scope1.first,
            scope1.second,
            scope2.first,
            scope2.second,
            layouts.first,
            layouts.second,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            image,
            subresource
        };
        return barrier;
    }
} // namespace Engine
