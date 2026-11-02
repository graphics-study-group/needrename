#ifndef ENGINE_RHI_VULKANIMAGEUTILS_INCLUDED
#define ENGINE_RHI_VULKANIMAGEUTILS_INCLUDED

#include "Rhi/Pipeline/PipelineDescription.h"
#include "Rhi/Texture/ImageUtils.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {

    constexpr bool HasColorAspect(ImageFormat format) {
        switch (format) {
        case ImageFormat::R8G8B8A8SNorm:
        case ImageFormat::R8G8B8A8UNorm:
        case ImageFormat::R8G8B8A8SRGB:
        case ImageFormat::R11G11B10UFloat:
        case ImageFormat::R32G32B32A32SFloat:
            return true;
        default:
            return false;
        }
    }

// HasDepthAspect moved to Rhi/Texture/ImageUtils.h: the render graph asks it on both backends.

    constexpr bool HasStencilAspect(ImageFormat format) {
        switch (format) {
        default:
            return false;
        }
    }

// CanCompressToBasis moved to Rhi/Texture/ImageUtils.h: it is a property of the format.

    /// @brief Convert a portable image aspect to the Vulkan flag it names.
    constexpr vk::ImageAspectFlags GetVkAspect(ImageAspect aspect) {
        vk::ImageAspectFlags flags{};
        if ((static_cast<uint8_t>(aspect) & static_cast<uint8_t>(ImageAspect::Color)) != 0) {
            flags |= vk::ImageAspectFlagBits::eColor;
        }
        if ((static_cast<uint8_t>(aspect) & static_cast<uint8_t>(ImageAspect::Depth)) != 0) {
            flags |= vk::ImageAspectFlagBits::eDepth;
        }
        if ((static_cast<uint8_t>(aspect) & static_cast<uint8_t>(ImageAspect::Stencil)) != 0) {
            flags |= vk::ImageAspectFlagBits::eStencil;
        }
        return flags;
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
        case ImageFormat::R11G11B10UFloat:
            return vk::Format::eB10G11R11UfloatPack32;
        case ImageFormat::R32G32B32A32SFloat:
            return vk::Format::eR32G32B32A32Sfloat;
        case ImageFormat::D32SFLOAT:
            return vk::Format::eD32Sfloat;
        case ImageFormat::R32SFloat:
            return vk::Format::eR32Sfloat;
        case ImageFormat::R32G32SFloat:
            return vk::Format::eR32G32Sfloat;
        case ImageFormat::R32G32B32SFloat:
            return vk::Format::eR32G32B32Sfloat;
        case ImageFormat::R32UInt:
            return vk::Format::eR32Uint;
        case ImageFormat::R16G16UInt:
            return vk::Format::eR16G16Uint;
        case ImageFormat::R8G8B8A8UInt:
            return vk::Format::eR8G8B8A8Uint;
        case ImageFormat::B8G8R8A8UNorm:
            return vk::Format::eB8G8R8A8Unorm;
        case ImageFormat::B8G8R8A8SRGB:
            return vk::Format::eB8G8R8A8Srgb;
        case ImageFormat::R8G8B8UNorm:
            return vk::Format::eR8G8B8Unorm;
        case ImageFormat::R8G8B8SRGB:
            return vk::Format::eR8G8B8Srgb;
        }
        return vk::Format::eUndefined;
    }

    /// @brief Convert a portable vertex format to the Vulkan format that carries it.
    constexpr vk::Format GetVkVertexFormat(VertexFormat format) {
        switch (format) {
        case VertexFormat::Float32x1:
            return vk::Format::eR32Sfloat;
        case VertexFormat::Float32x2:
            return vk::Format::eR32G32Sfloat;
        case VertexFormat::Float32x3:
            return vk::Format::eR32G32B32Sfloat;
        case VertexFormat::Float32x4:
            return vk::Format::eR32G32B32A32Sfloat;
        case VertexFormat::UInt8x4:
            return vk::Format::eR8G8B8A8Uint;
        case VertexFormat::UInt16x2:
            return vk::Format::eR16G16Uint;
        case VertexFormat::UInt32x1:
            return vk::Format::eR32Uint;
        case VertexFormat::Undefined:
        default:
            return vk::Format::eUndefined;
        }
    }

    constexpr ImageFormat FromVkFormat(vk::Format format) {
        switch (format) {
        case vk::Format::eR8G8B8A8Snorm:
            return ImageFormat::R8G8B8A8SNorm;
        case vk::Format::eR8G8B8A8Unorm:
            return ImageFormat::R8G8B8A8UNorm;
        case vk::Format::eR8G8B8A8Srgb:
            return ImageFormat::R8G8B8A8SRGB;
        case vk::Format::eB10G11R11UfloatPack32:
            return ImageFormat::R11G11B10UFloat;
        case vk::Format::eR32G32B32A32Sfloat:
            return ImageFormat::R32G32B32A32SFloat;
        case vk::Format::eD32Sfloat:
            return ImageFormat::D32SFLOAT;
        case vk::Format::eR32Sfloat:
            return ImageFormat::R32SFloat;
        case vk::Format::eR32G32Sfloat:
            return ImageFormat::R32G32SFloat;
        case vk::Format::eR32G32B32Sfloat:
            return ImageFormat::R32G32B32SFloat;
        case vk::Format::eR32Uint:
            return ImageFormat::R32UInt;
        case vk::Format::eR16G16Uint:
            return ImageFormat::R16G16UInt;
        case vk::Format::eR8G8B8A8Uint:
            return ImageFormat::R8G8B8A8UInt;
        case vk::Format::eB8G8R8A8Unorm:
            return ImageFormat::B8G8R8A8UNorm;
        case vk::Format::eB8G8R8A8Srgb:
            return ImageFormat::B8G8R8A8SRGB;
        case vk::Format::eR8G8B8Unorm:
            return ImageFormat::R8G8B8UNorm;
        case vk::Format::eR8G8B8Srgb:
            return ImageFormat::R8G8B8SRGB;
        default:
            return ImageFormat::UNDEFINED;
        }
    }

    /// @brief Convert the portable image type to its Vulkan equivalent.
    constexpr vk::ImageType GetVkImageType(ImageType type) {
        switch (type) {
        case ImageType::D1:
            return vk::ImageType::e1D;
        case ImageType::D3:
            return vk::ImageType::e3D;
        case ImageType::D2:
        default:
            return vk::ImageType::e2D;
        }
    }

    /// @brief Convert the portable extent to its Vulkan equivalent.
    constexpr vk::Extent3D GetVkExtent3D(Extent3D extent) {
        return vk::Extent3D{extent.width, extent.height, extent.depth};
    }

    /// @brief Convert the portable sample count to its Vulkan equivalent.
    constexpr vk::SampleCountFlagBits GetVkSampleCount(SampleCountBits samples) {
        return static_cast<vk::SampleCountFlagBits>(static_cast<uint32_t>(samples));
    }

    /// @brief Convert a capability query to the Vulkan feature bit it means.
    constexpr vk::FormatFeatureFlagBits GetVkFormatFeature(FormatFeature feature) {
        switch (feature) {
        case FormatFeature::VertexBuffer:
            return vk::FormatFeatureFlagBits::eVertexBuffer;
        case FormatFeature::StorageImageAtomic:
            return vk::FormatFeatureFlagBits::eStorageImageAtomic;
        case FormatFeature::StorageImage:
        default:
            return vk::FormatFeatureFlagBits::eStorageImage;
        }
    }

    // GetPixelSize used to live here. It is a property of the format rather than of the
    // graphics API, so it is in Rhi/Texture/ImageUtils.h now, where the second backend
    // can reach it too. The table there also covers formats this one omitted, including
    // the 12-byte R32G32B32SFloat that used to return zero.

    constexpr bool IsBlockCompressed(ImageFormat format) {
        switch (format) {
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
        vk::ImageViewType view_type{};
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
                (void)view_type;
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
#if defined(_MSC_VER)
        __assume(false);
#else
        __builtin_unreachable();
#endif
    }

    constexpr vk::SamplerMipmapMode ToVkSamplerMipmapMode(SamplerDesc::FilterMode filter) {
        using Mode = SamplerDesc::FilterMode;
        switch (filter) {
        case Mode::Linear:
            return vk::SamplerMipmapMode::eLinear;
        case Mode::Point:
            return vk::SamplerMipmapMode::eNearest;
        }
#if defined(_MSC_VER)
        __assume(false);
#else
        __builtin_unreachable();
#endif
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
#if defined(_MSC_VER)
        __assume(false);
#else
        __builtin_unreachable();
#endif
    }

    constexpr vk::BorderColor ToVkBorderColor(SamplerDesc::AddressMode addr, bool integer_variant = false) {
        using Mode = SamplerDesc::AddressMode;
        switch (addr) {
        case Mode::Repeat:
        case Mode::MirroredRepeat:
        case Mode::ClampToEdge:
            return vk::BorderColor::eFloatOpaqueBlack;
        case Mode::ClampToBorder_TransparentBlack:
            return integer_variant ? vk::BorderColor::eIntTransparentBlack : vk::BorderColor::eFloatTransparentBlack;
        case Mode::ClampToBorder_OpaqueBlack:
            return integer_variant ? vk::BorderColor::eIntOpaqueBlack : vk::BorderColor::eFloatOpaqueBlack;
        case Mode::ClampToBorder_OpaqueWhite:
            return integer_variant ? vk::BorderColor::eIntOpaqueWhite : vk::BorderColor::eFloatOpaqueWhite;
        }
#if defined(_MSC_VER)
        __assume(false);
#else
        __builtin_unreachable();
#endif
    }
} // namespace Engine::Rhi

#endif // ENGINE_RHI_VULKANIMAGEUTILS_INCLUDED








