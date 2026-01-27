#include "RenderTargetTexture.h"

namespace Engine {
    RenderTargetTexture::RenderTargetTexture(
        RenderSystem &system,
        TextureDesc texture,
        SamplerDesc sampler,
        const std::string & name
    ) : Texture(system, texture, sampler, name) {
        
    }
    RenderTargetTexture RenderTargetTexture::Create(
        RenderSystem &system, RenderTargetTextureDesc texture, SamplerDesc sampler, const std::string &name
    ) {
        assert(texture.multisample == 1 && "Unimplemented multisampling feature.");
        auto ret = RenderTargetTexture(system,
                TextureDesc{
                    .dimensions = texture.dimensions,
                    .width = texture.width,
                    .height = texture.height,
                    .depth = texture.depth,
                    .format = static_cast<ImageUtils::ImageFormat>(static_cast<int>(texture.format)),
                    .memory_type = {(texture.format == RenderTargetTextureDesc::RTTFormat::D32SFLOAT) 
                        ? ImageMemoryTypeBits::DefaultDepthAttachment : ImageMemoryTypeBits::DefaultColorAttachment},
                    .mipmap_levels = texture.mipmap_levels,
                    .array_layers = texture.array_layers,
                    .is_cube_map = texture.is_cube_map
                },
                sampler,
                name);

        ret.support_random_access = system.GetAllocatorState().QueryFormatFeatures(
            ImageUtils::GetVkFormat(ret.GetTextureDescription().format),
            vk::FormatFeatureFlagBits::eStorageImage
        );

        ret.support_atomic_access = system.GetAllocatorState().QueryFormatFeatures(
            ImageUtils::GetVkFormat(ret.GetTextureDescription().format),
            vk::FormatFeatureFlagBits::eStorageImageAtomic
        );

        return ret;
    }
    std::unique_ptr<RenderTargetTexture> RenderTargetTexture::CreateUnique(
        RenderSystem &system, RenderTargetTextureDesc texture, SamplerDesc sampler, const std::string &name
    ) {
        auto ret = std::unique_ptr<RenderTargetTexture>(
            new RenderTargetTexture(system,
                TextureDesc{
                    .dimensions = texture.dimensions,
                    .width = texture.width,
                    .height = texture.height,
                    .depth = texture.depth,
                    .format = static_cast<ImageUtils::ImageFormat>(static_cast<int>(texture.format)),
                    .memory_type = {(texture.format == RenderTargetTextureDesc::RTTFormat::D32SFLOAT) 
                        ? ImageMemoryTypeBits::DefaultDepthAttachment : ImageMemoryTypeBits::DefaultColorAttachment},
                    .mipmap_levels = texture.mipmap_levels,
                    .array_layers = texture.array_layers,
                    .is_cube_map = texture.is_cube_map
                },
                sampler,
                name
            )
        );
        ret->support_random_access = system.GetAllocatorState().QueryFormatFeatures(
            ImageUtils::GetVkFormat(ret->GetTextureDescription().format),
            vk::FormatFeatureFlagBits::eStorageImage
        );

        ret->support_atomic_access = system.GetAllocatorState().QueryFormatFeatures(
            ImageUtils::GetVkFormat(ret->GetTextureDescription().format),
            vk::FormatFeatureFlagBits::eStorageImageAtomic
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
