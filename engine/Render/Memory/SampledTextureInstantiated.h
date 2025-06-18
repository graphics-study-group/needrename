#ifndef RENDER_MEMORY_SAMPLEDTEXTUREINSTANTIATED_INCLUDED
#define RENDER_MEMORY_SAMPLEDTEXTUREINSTANTIATED_INCLUDED

#include "SampledTexture.h"
#include "Asset/InstantiatedFromAsset.h"
#include "Asset/Texture/Image2DTextureAsset.h"

namespace Engine {
    /**
     * @brief Sampled texture instantiated from `TextureAsset` asset.
     * Only texture and sampler descriptions are loaded, and actual pixel data must be
     * uploaded to device memory with `EnqueueTextureBufferSubmission()`.
     */
    class SampledTextureInstantiated : public SampledTexture, public IInstantiatedFromAsset<Image2DTextureAsset> {
    public:
        SampledTextureInstantiated(RenderSystem & system);

        void Instantiate(const Image2DTextureAsset & asset) override;
    };
}

#endif // RENDER_MEMORY_SAMPLEDTEXTUREINSTANTIATED_INCLUDED
