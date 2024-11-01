#ifndef RENDER_MATERIAL_TESTMATERIALWITHSAMPLER_INCLUDED
#define RENDER_MATERIAL_TESTMATERIALWITHSAMPLER_INCLUDED

#include "Render/Material/Material.h"
#include "Render/Pipeline/Shader.h"

namespace Engine {
    class ShaderModule;
    class AllocatedImage2DTexture;

    class TestMaterialWithSampler : public Material {
        vk::UniqueSampler m_sampler {};
        ShaderModule fragModule;
        ShaderModule vertModule;
    public:
        TestMaterialWithSampler (
            std::weak_ptr <RenderSystem> system,
            const AllocatedImage2DTexture & texture
        );

        const virtual Pipeline * GetPipeline(uint32_t pass_index) override;
    };
}


#endif // RENDER_MATERIAL_TESTMATERIALWITHSAMPLER_INCLUDED
