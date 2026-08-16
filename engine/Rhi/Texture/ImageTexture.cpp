#include "Rhi/Texture/ImageTexture.h"

namespace Engine::Rhi {
    ImageTexture::ImageTexture(
        DeviceContext &device_context, TextureDesc texture, SamplerDesc sampler, const std::string &name
    ) : Texture(device_context, texture, sampler, name) {
    }

    std::unique_ptr<ImageTexture> ImageTexture::CreateUnique(
        DeviceContext &device_context, ImageTextureDesc texture, SamplerDesc sampler, const std::string &name
    ) {
        return std::unique_ptr<ImageTexture>(new ImageTexture(
            device_context,
            TextureDesc{
                .dimensions = texture.dimensions,
                .width = texture.width,
                .height = texture.height,
                .depth = texture.depth,
                .format = static_cast<Rhi::ImageFormat>(static_cast<int>(texture.format)),
                .memory_type = {ImageMemoryTypeBits::DefaultTexture},
                .mipmap_levels = texture.mipmap_levels,
                .array_layers = texture.array_layers,
                .is_cube_map = texture.is_cube_map
            },
            sampler,
            name
        ));
    }
} // namespace Engine::Rhi
