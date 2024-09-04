#ifndef RENDER_MATERIAL_TESTMATERIAL_INCLUDED
#define RENDER_MATERIAL_TESTMATERIAL_INCLUDED

#include "Render/Material/Material.h"
#include "Render/Pipeline/Shader.h"

namespace Engine {
    class ShaderModule;
    class RenderPass;

    class TestMaterialWithTransform : public Material {
        ShaderModule fragModule;
        ShaderModule vertModule;
    public:
        TestMaterialWithTransform (
            std::weak_ptr <RenderSystem> system, 
            const RenderPass & pass
        );
    };
}

#endif // RENDER_MATERIAL_TESTMATERIAL_INCLUDED
