#include "RenderTargetTexture.h"

#include "Rhi/Device/AllocatorState.h"
#include <Rhi/Device/DeviceContext.h>

namespace Engine {
    RenderTargetTexture::RenderTargetTexture(
        Rhi::DeviceContext &device_context, Rhi::TextureDesc texture, Rhi::SamplerDesc sampler, const std::string &name
    ) : Rhi::Texture(device_context, texture, sampler, name) {
    }
    RenderTargetTexture RenderTargetTexture::Create(
        Rhi::DeviceContext &device_context,
        RenderTargetTextureDesc texture,
        Rhi::SamplerDesc sampler,
        const std::string &name
    ) {
        assert(texture.multisample == 1 && "Unimplemented multisampling feature.");
        auto ret = RenderTargetTexture(
            device_context,
            Rhi::TextureDesc{
                .dimensions = texture.dimensions,
                .width = texture.width,
                .height = texture.height,
                .depth = texture.depth,
                .format = static_cast<Rhi::ImageFormat>(static_cast<int>(texture.format)),
                .memory_type =
                    {(texture.format == RenderTargetTextureDesc::RTTFormat::D32SFLOAT)
                         ? Rhi::ImageMemoryTypeBits::DefaultDepthAttachment
                         : Rhi::ImageMemoryTypeBits::DefaultColorAttachment},
                .mipmap_levels = texture.mipmap_levels,
                .array_layers = texture.array_layers,
                .is_cube_map = texture.is_cube_map
            },
            sampler,
            name
        );

        ret.support_random_access = device_context.GetAllocatorState().QueryFormatFeatures(
            Rhi::GetVkFormat(ret.GetTextureDescription().format), vk::FormatFeatureFlagBits::eStorageImage
        );

        ret.support_atomic_access = device_context.GetAllocatorState().QueryFormatFeatures(
            Rhi::GetVkFormat(ret.GetTextureDescription().format), vk::FormatFeatureFlagBits::eStorageImageAtomic
        );

        return ret;
    }
    std::unique_ptr<RenderTargetTexture> RenderTargetTexture::CreateUnique(
        Rhi::DeviceContext &device_context,
        RenderTargetTextureDesc texture,
        Rhi::SamplerDesc sampler,
        const std::string &name
    ) {
        auto ret = std::unique_ptr<RenderTargetTexture>(new RenderTargetTexture(
            device_context,
            Rhi::TextureDesc{
                .dimensions = texture.dimensions,
                .width = texture.width,
                .height = texture.height,
                .depth = texture.depth,
                .format = static_cast<Rhi::ImageFormat>(static_cast<int>(texture.format)),
                .memory_type =
                    {(texture.format == RenderTargetTextureDesc::RTTFormat::D32SFLOAT)
                         ? Rhi::ImageMemoryTypeBits::DefaultDepthAttachment
                         : Rhi::ImageMemoryTypeBits::DefaultColorAttachment},
                .mipmap_levels = texture.mipmap_levels,
                .array_layers = texture.array_layers,
                .is_cube_map = texture.is_cube_map
            },
            sampler,
            name
        ));
        ret->support_random_access = device_context.GetAllocatorState().QueryFormatFeatures(
            Rhi::GetVkFormat(ret->GetTextureDescription().format), vk::FormatFeatureFlagBits::eStorageImage
        );

        ret->support_atomic_access = device_context.GetAllocatorState().QueryFormatFeatures(
            Rhi::GetVkFormat(ret->GetTextureDescription().format), vk::FormatFeatureFlagBits::eStorageImageAtomic
        );
        return ret;
    }
    bool RenderTargetTexture::SupportRandomAccess() const noexcept {
        return support_random_access;
    }
    bool RenderTargetTexture::SupportAtomicOperation() const noexcept {
        return support_atomic_access;
    }
} // namespace Engine
