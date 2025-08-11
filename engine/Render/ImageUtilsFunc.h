#ifndef RENDER_IMAGEUTILSFUNC
#define RENDER_IMAGEUTILSFUNC

#include "ImageUtils.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace Engine {
    namespace ImageUtils {
        constexpr std::tuple<vk::ImageUsageFlags, VmaMemoryUsage> GetImageFlags(ImageType type) {
            switch (type) {
            case ImageType::DepthImage:
            case ImageType::DepthStencilImage:
                return std::make_tuple(
                    vk::ImageUsageFlagBits::eDepthStencilAttachment, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                );
            case ImageType::SampledDepthImage:
                return std::make_tuple(
                    vk::ImageUsageFlags{
                        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled
                    },
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                );
            case ImageType::TextureImage:
                return std::make_tuple(
                    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
                        | vk::ImageUsageFlagBits::eSampled,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                );
            case ImageType::ColorAttachment:
                return std::make_tuple(
                    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
                        | vk::ImageUsageFlagBits::eColorAttachment,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                );
            case ImageType::ColorCompute:
                return std::make_tuple(
                    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
                        | vk::ImageUsageFlagBits::eStorage,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                );
            case ImageType::ColorGeneral:
                return std::make_tuple(
                    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
                        | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage
                        | vk::ImageUsageFlagBits::eColorAttachment,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                );
            }
            return std::make_tuple(vk::ImageUsageFlags{}, VMA_MEMORY_USAGE_AUTO);
        }

        constexpr vk::ImageAspectFlags GetVkImageAspect(ImageType type) {
            switch (type) {
            case ImageType::DepthImage:
            case ImageType::SampledDepthImage:
                return vk::ImageAspectFlagBits::eDepth;
            case ImageType::DepthStencilImage:
                return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
            case ImageType::TextureImage:
            case ImageType::ColorAttachment:
            case ImageType::ColorCompute:
            case ImageType::ColorGeneral:
                return vk::ImageAspectFlagBits::eColor;
            }
            return vk::ImageAspectFlags{};
        }

        constexpr vk::ImageAspectFlags GetVkAspect(ImageFormat format) {
            switch (format) {
            case ImageFormat::R8G8B8SRGB:
            case ImageFormat::B8G8R8A8SRGB:
            case ImageFormat::R8G8B8A8SRGB:
            case ImageFormat::R8G8B8A8SNorm:
            case ImageFormat::R8G8B8A8UNorm:
            case ImageFormat::R11G11B10UFloat:
                return vk::ImageAspectFlagBits::eColor;
            case ImageFormat::D32SFLOAT:
                return vk::ImageAspectFlagBits::eDepth;
            default:
                return vk::ImageAspectFlagBits::eNone;
            }
        }

        constexpr vk::ImageType GetVkTypeFromExtent(VkExtent3D extent) {
            assert(extent.width > 0 && extent.height > 0 && extent.depth > 0);
            if (extent.height == 1) {
                return vk::ImageType::e1D;
            } else if (extent.depth == 1) {
                return vk::ImageType::e2D;
            } else {
                return vk::ImageType::e3D;
            }
        }

        constexpr vk::Format GetVkFormat(ImageFormat format) {
            switch (format) {
            case ImageFormat::UNDEFINED:
                return vk::Format::eUndefined;
            case ImageFormat::B8G8R8A8SRGB:
                return vk::Format::eB8G8R8A8Srgb;
            case ImageFormat::R8G8B8A8SRGB:
                return vk::Format::eR8G8B8A8Srgb;
            case ImageFormat::R8G8B8A8SNorm:
                return vk::Format::eR8G8B8A8Snorm;
            case ImageFormat::R8G8B8A8UNorm:
                return vk::Format::eR8G8B8A8Unorm;
            case ImageFormat::R11G11B10UFloat:
                return vk::Format::eB10G11R11UfloatPack32;
            case ImageFormat::R8G8B8SRGB:
                return vk::Format::eR8G8B8Srgb;
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
            case ImageFormat::R8G8B8A8SNorm:
            case ImageFormat::R8G8B8A8UNorm:
            case ImageFormat::R11G11B10UFloat:
            case ImageFormat::D32SFLOAT:
                return 4;
            default:
                return 0;
            }
        }

        constexpr vk::ImageViewType InferImageViewType(const TextureDesc &desc) {
            vk::ImageViewType view_type;
            if (desc.is_cube_map) {
                assert(desc.array_layers > 0 && desc.array_layers % 6 == 0);
                view_type = desc.array_layers > 6 ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube;
            } else {
                switch (desc.dimensions) {
                case 1:
                    view_type = desc.array_layers > 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
                    break;
                case 2:
                    view_type = desc.array_layers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
                    break;
                case 3:
                    assert(desc.array_layers == 1);
                    view_type = vk::ImageViewType::e3D;
                    break;
                default:
                    assert(!"Cannot construct an texture image on spaces that cannot be embedded into 3D Riemmanian "
                            "manifolds.");
                }
            }
            return view_type;
        }
    } // namespace ImageUtils
} // namespace Engine

#endif // RENDER_IMAGEUTILSFUNC
