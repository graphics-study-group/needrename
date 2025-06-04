#ifndef RENDER_MEMORY_SAMPLEDTEXTUREINSTANTIATED_INCLUDED
#define RENDER_MEMORY_SAMPLEDTEXTUREINSTANTIATED_INCLUDED

#include "SampledTexture.h"

namespace Engine {
    class Image2DTextureAsset;

    /**
     * @brief Sampled texture instantiated from `TextureAsset` asset.
     * Only texture and sampler descriptions are loaded, and actual pixel data must be
     * uploaded to device memory with `EnqueueTextureBufferSubmission()`.
     */
    class SampledTextureInstantiated : public SampledTexture {
    public:
        SampledTextureInstantiated(RenderSystem & system);

        void Instantiate(const Image2DTextureAsset & asset);
    };
}

#endif // RENDER_MEMORY_SAMPLEDTEXTUREINSTANTIATED_INCLUDED
