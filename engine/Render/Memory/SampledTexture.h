#ifndef RENDER_MEMORY_SAMPLEDTEXTURE_INCLUDED
#define RENDER_MEMORY_SAMPLEDTEXTURE_INCLUDED

#include "Texture.h"

namespace Engine {
    class SampledTexture : public Texture {
    public:
        using Texture::TextureDesc;
        struct SamplerDesc {

        };
    protected:
        SamplerDesc m_sampler_desc;
        // TODO: We need to allocate the sampler from a pool instead of creating it each time.
        vk::UniqueSampler m_sampler;

    public:
        SampledTexture(RenderSystem & system) noexcept;
        
        void CreateTextureAndSampler(TextureDesc textureDesc, SamplerDesc samplerDesc, std::string name);
        void CreateSampler(SamplerDesc samplerDesc);
        
        const SamplerDesc & GetSamplerDesc() const noexcept;
        vk::Sampler GetSampler() const noexcept;
    };
}

#endif // RENDER_MEMORY_SAMPLEDTEXTURE_INCLUDED
