#ifndef RENDER_MATERIAL_TESTMATERIAL_INCLUDED
#define RENDER_MATERIAL_TESTMATERIAL_INCLUDED

#include "Render/Material/Material.h"
#include "Render/Pipeline/Shader.h"

namespace Engine {
    class ShaderModule;

    class TestMaterial : public Material {
        ShaderModule fragModule;
        ShaderModule vertModule;
    public:
        TestMaterial (
            std::weak_ptr <RenderSystem> system
        );

        const virtual Pipeline * GetPipeline(uint32_t pass_index) override;
        const virtual Pipeline * GetSkinnedPipeline(uint32_t pass_index) override;
    };
}

#endif // RENDER_MATERIAL_TESTMATERIAL_INCLUDED
