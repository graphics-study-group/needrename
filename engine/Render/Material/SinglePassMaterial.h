#ifndef RENDER_MATERIAL_SINGLEPASSMATERIAL_INCLUDED
#define RENDER_MATERIAL_SINGLEPASSMATERIAL_INCLUDED

#include "Material.h"
#include "Render/NativeResource/ShaderPass.h"
#include <glad/glad.h>

namespace Engine
{
    class SinglePassMaterial : public Material {
    public:
        SinglePassMaterial (std::shared_ptr<RenderSystem> system, std::shared_ptr<ShaderPass> pass);
        virtual ~SinglePassMaterial();
        void virtual PrepareDraw(const MaterialDrawContext* context) override;
    protected:
        std::shared_ptr <ShaderPass> m_pass;
    };
} // namespace Engine


#endif // RENDER_MATERIAL_SINGLEPASSMATERIAL_INCLUDED
