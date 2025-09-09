#include "SampledTextureInstantiated.h"

#include "Render/RenderSystem/SamplerManager.h"
#include "Asset/Texture/Image2DTextureAsset.h"

namespace Engine {
    SampledTextureInstantiated::SampledTextureInstantiated(RenderSystem &system) : SampledTexture(system) {
    }
    void SampledTextureInstantiated::Instantiate(const Image2DTextureAsset &asset) {
        TextureDesc tDesc{
            .dimensions = 2,
            .width = asset.m_width,
            .height = asset.m_height,
            .depth = 1,
            .format = asset.m_format,
            .type = ImageUtils::ImageType::TextureImage,
            .mipmap_levels = asset.m_mip_level,
            .array_layers = 1,
            .is_cube_map = false
        };
        SamplerDesc sDesc{

        };
        this->CreateTextureAndSampler(tDesc, sDesc, std::format("Image - asset {}", asset.m_name));
    }
} // namespace Engine
