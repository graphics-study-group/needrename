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
        using SamplerDesc = ImageUtils::SamplerDesc;

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
