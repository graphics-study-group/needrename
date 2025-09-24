#include "ImageTexture.h"

#include "Asset/Texture/Image2DTextureAsset.h"

namespace Engine {
    ImageTexture::ImageTexture(
        RenderSystem &system, 
        TextureDesc texture, 
        SamplerDesc sampler,
        const std::string & name
    ) : Texture(
        system, texture, sampler, name
        ) {
    }

    std::unique_ptr<ImageTexture> ImageTexture::CreateUnique(
        RenderSystem &system, ImageTextureDesc texture, SamplerDesc sampler, const std::string &name
    ) {
        return std::unique_ptr<ImageTexture>(new ImageTexture(
                system,
                TextureDesc{
                    .dimensions = texture.dimensions,
                    .width = texture.width,
                    .height = texture.height,
                    .depth = texture.depth,
                    .format = static_cast<ImageUtils::ImageFormat>(static_cast<int>(texture.format)),
                    .type = ImageUtils::ImageType::TextureImage,
                    .mipmap_levels = texture.mipmap_levels,
                    .array_layers = texture.array_layers,
                    .is_cube_map = texture.is_cube_map
                }, sampler, name
            )
        );
    }
    std::unique_ptr<ImageTexture> ImageTexture::CreateUnique(RenderSystem &system, const Image2DTextureAsset &asset) {
        return std::unique_ptr<ImageTexture>(new ImageTexture(
                system, 
                TextureDesc{
                    .dimensions = 2,
                    .width = static_cast<uint32_t>(asset.m_width),
                    .height = static_cast<uint32_t>(asset.m_height),
                    .depth = 1,
                    .format = ImageUtils::ImageFormat::R8G8B8A8SRGB,
                    .type = ImageUtils::ImageType::TextureImage,
                    .mipmap_levels = asset.m_mip_level,
                    .array_layers = 1,
                    .is_cube_map = false
                }, SamplerDesc{}, asset.m_name
            )
        );
    }
} // namespace Engine
