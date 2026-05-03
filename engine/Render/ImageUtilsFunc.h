#ifndef ENGINE_RENDER_IMAGEUTILSFUNC_INCLUDED
#define ENGINE_RENDER_IMAGEUTILSFUNC_INCLUDED

#include "ImageUtils.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace Engine {
    namespace ImageUtils {

        constexpr bool HasColorAspect(ImageFormat format) {
            switch (format) {
            case ImageFormat::R8G8B8A8SNorm:
            case ImageFormat::R8G8B8A8UNorm:
            case ImageFormat::R8G8B8A8SRGB:
            case ImageFormat::BC7UNorm:
            case ImageFormat::BC7SRGB:
            case ImageFormat::R11G11B10UFloat:
            case ImageFormat::R32G32B32A32SFloat:
                return true;
            default:
                return false;
            }
        }

        constexpr bool HasDepthAspect(ImageFormat format) {
            switch (format) {
            case ImageFormat::D32SFLOAT:
                return true;
            default:
                return false;
            }
        }

        constexpr bool HasStencilAspect(ImageFormat format) {
            switch (format) {
            default:
                return false;
            }
        }

        constexpr bool CanCompressToBc7(ImageFormat format) {
            return format == ImageFormat::R8G8B8A8UNorm
                   || format == ImageFormat::R8G8B8A8SRGB;
        }

        constexpr vk::ImageAspectFlags GetVkAspect(ImageFormat format) {
            return (HasColorAspect(format) ? vk::ImageAspectFlagBits::eColor : vk::ImageAspectFlagBits::eNone)
                   | (HasDepthAspect(format) ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eNone)
                   | (HasStencilAspect(format) ? vk::ImageAspectFlagBits::eStencil : vk::ImageAspectFlagBits::eNone);
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
            case ImageFormat::R8G8B8A8SNorm:
                return vk::Format::eR8G8B8A8Snorm;
            case ImageFormat::R8G8B8A8UNorm:
                return vk::Format::eR8G8B8A8Unorm;
            case ImageFormat::R8G8B8A8SRGB:
                return vk::Format::eR8G8B8A8Srgb;
            case ImageFormat::BC7UNorm:
                return vk::Format::eBc7UnormBlock;
            case ImageFormat::BC7SRGB:
                return vk::Format::eBc7SrgbBlock;
            case ImageFormat::R11G11B10UFloat:
                return vk::Format::eB10G11R11UfloatPack32;
            case ImageFormat::R32G32B32A32SFloat:
                return vk::Format::eR32G32B32A32Sfloat;
            case ImageFormat::D32SFLOAT:
                return vk::Format::eD32Sfloat;
            }
            return vk::Format::eUndefined;
        }

        constexpr ImageFormat FromVkFormat(vk::Format format) {
            switch (format) {
            case vk::Format::eR8G8B8A8Snorm:
                return ImageFormat::R8G8B8A8SNorm;
            case vk::Format::eR8G8B8A8Unorm:
                return ImageFormat::R8G8B8A8UNorm;
            case vk::Format::eR8G8B8A8Srgb:
                return ImageFormat::R8G8B8A8SRGB;
            case vk::Format::eBc7UnormBlock:
                return ImageFormat::BC7UNorm;
            case vk::Format::eBc7SrgbBlock:
                return ImageFormat::BC7SRGB;
            case vk::Format::eB10G11R11UfloatPack32:
                return ImageFormat::R11G11B10UFloat;
            case vk::Format::eR32G32B32A32Sfloat:
                return ImageFormat::R32G32B32A32SFloat;
            case vk::Format::eD32Sfloat:
                return ImageFormat::D32SFLOAT;
            default:
                return ImageFormat::UNDEFINED;
            }
        }

        constexpr uint16_t GetPixelSize(ImageFormat format) {
            switch (format) {
            case ImageFormat::R8G8B8A8SNorm:
            case ImageFormat::R8G8B8A8UNorm:
            case ImageFormat::R8G8B8A8SRGB:
            case ImageFormat::R11G11B10UFloat:
            case ImageFormat::D32SFLOAT:
                return 4;
            case ImageFormat::R32G32B32A32SFloat:
                return 16;
            case ImageFormat::BC7UNorm:
            case ImageFormat::BC7SRGB:
                return 0;
            default:
                return 0;
            }
        }

        constexpr bool IsBlockCompressed(ImageFormat format) {
            switch (format) {
            case ImageFormat::BC7UNorm:
            case ImageFormat::BC7SRGB:
                return true;
            default:
                return false;
            }
        }

        constexpr uint64_t GetImageDataSize(
            ImageFormat format, uint32_t width, uint32_t height, uint32_t depth, uint32_t array_layers
        ) {
            if (IsBlockCompressed(format)) {
                const uint64_t block_width = (static_cast<uint64_t>(width) + 3ull) / 4ull;
                const uint64_t block_height = (static_cast<uint64_t>(height) + 3ull) / 4ull;
                constexpr uint64_t block_byte_size = 16ull;
                return block_width * block_height * static_cast<uint64_t>(depth) * static_cast<uint64_t>(array_layers)
                       * block_byte_size;
            }

            return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * static_cast<uint64_t>(depth)
                   * static_cast<uint64_t>(array_layers) * static_cast<uint64_t>(GetPixelSize(format));
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

        constexpr vk::Filter ToVkFilter(SamplerDesc::FilterMode filter) {
            using Mode = SamplerDesc::FilterMode;
            switch (filter) {
            case Mode::Linear:
                return vk::Filter::eLinear;
            case Mode::Point:
                return vk::Filter::eNearest;
            }
            __builtin_unreachable();
        }

        constexpr vk::SamplerMipmapMode ToVkSamplerMipmapMode(SamplerDesc::FilterMode filter) {
            using Mode = SamplerDesc::FilterMode;
            switch (filter) {
            case Mode::Linear:
                return vk::SamplerMipmapMode::eLinear;
            case Mode::Point:
                return vk::SamplerMipmapMode::eNearest;
            }
            __builtin_unreachable();
        }

        constexpr vk::SamplerAddressMode ToVkSamplerAddressMode(SamplerDesc::AddressMode addr) {
            using Mode = SamplerDesc::AddressMode;
            switch (addr) {
            case Mode::Repeat:
                return vk::SamplerAddressMode::eRepeat;
            case Mode::MirroredRepeat:
                return vk::SamplerAddressMode::eMirroredRepeat;
            case Mode::ClampToEdge:
                return vk::SamplerAddressMode::eClampToEdge;
            case Mode::ClampToBorder_TransparentBlack:
            case Mode::ClampToBorder_OpaqueBlack:
            case Mode::ClampToBorder_OpaqueWhite:
                return vk::SamplerAddressMode::eClampToBorder;
            }
            __builtin_unreachable();
        }

        constexpr vk::BorderColor ToVkBorderColor(SamplerDesc::AddressMode addr, bool integer_variant = false) {
            using Mode = SamplerDesc::AddressMode;
            switch (addr) {
            case Mode::Repeat:
            case Mode::MirroredRepeat:
            case Mode::ClampToEdge:
                return vk::BorderColor::eFloatOpaqueBlack;
            case Mode::ClampToBorder_TransparentBlack:
                return integer_variant ? vk::BorderColor::eIntTransparentBlack
                                       : vk::BorderColor::eFloatTransparentBlack;
            case Mode::ClampToBorder_OpaqueBlack:
                return integer_variant ? vk::BorderColor::eIntOpaqueBlack : vk::BorderColor::eFloatOpaqueBlack;
            case Mode::ClampToBorder_OpaqueWhite:
                return integer_variant ? vk::BorderColor::eIntOpaqueWhite : vk::BorderColor::eFloatOpaqueWhite;
            }
            __builtin_unreachable();
        }
    } // namespace ImageUtils
} // namespace Engine

#endif // ENGINE_RENDER_IMAGEUTILSFUNC_INCLUDED
