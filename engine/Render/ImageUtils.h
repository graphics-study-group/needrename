#ifndef ENGINE_RENDER_IMAGEUTILS_INCLUDED
#define ENGINE_RENDER_IMAGEUTILS_INCLUDED

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

namespace Engine {
    namespace ImageUtils {
        enum class ImageType {
            DepthImage,
            DepthStencilImage,
            TextureImage,
            ColorAttachment,
            // DepthStencilAttachment
        };

        enum class ImageFormat {
            UNDEFINED,
            B8G8R8A8SRGB,
            R8G8B8A8SRGB,
            R8G8B8SRGB,
            D32SFLOAT,
        };

        constexpr std::tuple<vk::ImageUsageFlags, VmaMemoryUsage> GetImageFlags(ImageType type)
        {
            switch(type) {
            case ImageType::DepthImage:
            case ImageType::DepthStencilImage:
                return std::make_tuple(
                    vk::ImageUsageFlagBits::eDepthStencilAttachment,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                );
            case ImageType::TextureImage:
                return std::make_tuple(
                    vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                );
            case ImageType::ColorAttachment:
                return std::make_tuple(
                    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eColorAttachment,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                );
            }
            return std::make_tuple(
                vk::ImageUsageFlags{},
                VMA_MEMORY_USAGE_AUTO
            );
        }

        constexpr vk::ImageAspectFlags GetVkImageAspect(ImageType type)
        {
            switch(type) {
                case ImageType::DepthImage:
                    return vk::ImageAspectFlagBits::eDepth;
                case ImageType::DepthStencilImage:
                    return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
                case ImageType::TextureImage:
                case ImageType::ColorAttachment:
                    return vk::ImageAspectFlagBits::eColor;
            }
            return vk::ImageAspectFlags{};
        }

        constexpr vk::ImageType GetVkTypeFromExtent(VkExtent3D extent)
        {
            assert(extent.width > 0 && extent.height > 0 && extent.depth > 0);
            if (extent.height == 1) {
                return vk::ImageType::e1D;
            } else if (extent.depth == 1) {
                return vk::ImageType::e2D;
            } else {
                return vk::ImageType::e3D;
            }
        }

        constexpr vk::Format GetVkFormat(ImageFormat format)
        {
            switch (format) {
                case ImageFormat::UNDEFINED:
                    return vk::Format::eUndefined;
                case ImageFormat::B8G8R8A8SRGB:
                    return vk::Format::eB8G8R8A8Srgb;
                case ImageFormat::R8G8B8SRGB:
                    return vk::Format::eR8G8B8Srgb;
                case ImageFormat::R8G8B8A8SRGB:
                    return vk::Format::eR8G8B8A8Srgb;
                case ImageFormat::D32SFLOAT:
                    return vk::Format::eD32Sfloat;
            }
            return vk::Format::eUndefined;
        }

        constexpr uint16_t GetPixelSize(ImageFormat format) {
            switch (format) {
                case ImageFormat::R8G8B8SRGB:
                    return 3;
                case ImageFormat::B8G8R8A8SRGB:
                case ImageFormat::R8G8B8A8SRGB:
                case ImageFormat::D32SFLOAT:
                    return 4;
                default:
                    return 0;
            }
        }
    }
}

#endif // ENGINE_RENDER_IMAGEUTILS_INCLUDED
