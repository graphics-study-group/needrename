#ifndef RENDER_MEMORY_SAMPLEDTEXTURE_INCLUDED
#define RENDER_MEMORY_SAMPLEDTEXTURE_INCLUDED

#include "Texture.h"

namespace vk {
    class Sampler;
}

namespace Engine {
    class SampledTexture : public Texture {
    public:
        using Texture::TextureDesc;
        struct SamplerDesc {
            enum class AddressMode {
                Repeat,
                MirroredRepeat,
                ClampToEdge
            };

            enum class FilterMode {
                Point,
                Linear
            };

            FilterMode min_filter{FilterMode::Point}, max_filter{FilterMode::Point}, mipmap_filter{FilterMode::Point};
            AddressMode u_address{AddressMode::Repeat}, v_address{AddressMode::Repeat}, w_address{AddressMode::Repeat};
            float biasLod{0.0}, minLod{0.0}, maxLod{0.0};
        };

    protected:
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        SampledTexture(RenderSystem &system) noexcept;
        virtual ~SampledTexture();

        void CreateTextureAndSampler(TextureDesc textureDesc, SamplerDesc samplerDesc, std::string name);
        void CreateSampler(SamplerDesc samplerDesc);

        const SamplerDesc &GetSamplerDesc() const noexcept;
        vk::Sampler GetSampler() const noexcept;
    };
} // namespace Engine

#endif // RENDER_MEMORY_SAMPLEDTEXTURE_INCLUDED
