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
        return RenderTargetTexture(system,
                TextureDesc{
                    .dimensions = texture.dimensions,
                    .width = texture.width,
                    .height = texture.height,
                    .depth = texture.depth,
                    .format = static_cast<ImageUtils::ImageFormat>(static_cast<int>(texture.format)),
                    .type = (texture.format == RenderTargetTextureDesc::RTTFormat::D32SFLOAT) 
                        ? ImageUtils::ImageType::DepthAttachment : ImageUtils::ImageType::ColorAttachment,
                    .mipmap_levels = texture.mipmap_levels,
                    .array_layers = texture.array_layers,
                    .is_cube_map = texture.is_cube_map
                },
                sampler,
                name);
    }
    std::unique_ptr<RenderTargetTexture> RenderTargetTexture::CreateUnique(
        RenderSystem &system, RenderTargetTextureDesc texture, SamplerDesc sampler, const std::string &name
    ) {
        
        return std::unique_ptr<RenderTargetTexture>(
            new RenderTargetTexture(system,
                TextureDesc{
                    .dimensions = texture.dimensions,
                    .width = texture.width,
                    .height = texture.height,
                    .depth = texture.depth,
                    .format = static_cast<ImageUtils::ImageFormat>(static_cast<int>(texture.format)),
                    .type = (texture.format == RenderTargetTextureDesc::RTTFormat::D32SFLOAT) 
                        ? ImageUtils::ImageType::DepthAttachment : ImageUtils::ImageType::ColorAttachment,
                    .mipmap_levels = texture.mipmap_levels,
                    .array_layers = texture.array_layers,
                    .is_cube_map = texture.is_cube_map
                },
                sampler,
                name
            )
        );
    }
} // namespace Engine
