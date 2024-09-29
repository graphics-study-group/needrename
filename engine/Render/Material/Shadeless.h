#ifndef RENDER_MATERIAL_SHADELESS_INCLUDED
#define RENDER_MATERIAL_SHADELESS_INCLUDED

#include "Render/Material/Material.h"
#include "Render/Pipeline/Shader.h"

namespace Engine {
    class ShaderModule;
    class RenderPass;
    class AllocatedImage2DTexture;

    class Shadeless : public Material {
        vk::UniqueSampler m_sampler {};
        ShaderModule fragModule;
        ShaderModule vertModule;
    public:
        Shadeless (std::weak_ptr <RenderSystem> system);
        Shadeless (
            std::weak_ptr <RenderSystem> system,
            const AllocatedImage2DTexture & texture
        );

        const virtual Pipeline * GetPipeline(uint32_t pass_index, const RenderTargetSetup & rts) override;

        void UpdateTexture(const AllocatedImage2DTexture & texture);
    };
}

#endif // RENDER_MATERIAL_SHADELESS_INCLUDED
