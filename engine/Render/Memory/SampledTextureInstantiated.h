#ifndef RENDER_MEMORY_SAMPLEDTEXTUREINSTANTIATED_INCLUDED
#define RENDER_MEMORY_SAMPLEDTEXTUREINSTANTIATED_INCLUDED

#include "Asset/InstantiatedFromAsset.h"
#include "SampledTexture.h"

namespace Engine {
    class Image2DTextureAsset;
    /**
     * @brief Sampled texture instantiated from `TextureAsset` asset.
     * Only texture and sampler
     * descriptions are loaded, and actual pixel data must be
     * uploaded to device memory with
     * `EnqueueTextureBufferSubmission()`.
     */
    /* class SampledTextureInstantiated : public SampledTexture, public IInstantiatedFromAsset<Image2DTextureAsset> {
    public:
        SampledTextureInstantiated(RenderSystem &system);

        void Instantiate(const Image2DTextureAsset &asset) override;
    }; */
} // namespace Engine

#endif // RENDER_MEMORY_SAMPLEDTEXTUREINSTANTIATED_INCLUDED
