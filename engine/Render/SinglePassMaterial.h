#ifndef RENDER_SINGLEPASSMATERIAL_INCLUDED
#define RENDER_SINGLEPASSMATERIAL_INCLUDED

#include "Material.h"
#include "NativeResource/ShaderPass.h"
#include <glad/glad.h>

namespace Engine
{
    class SinglePassMaterial : public Material {
    public:
        SinglePassMaterial (std::shared_ptr<RenderSystem> system, std::shared_ptr<ShaderPass> pass);
        virtual ~SinglePassMaterial();
        void virtual PrepareDraw(/*...*/) override;
    protected:
        std::shared_ptr <ShaderPass> m_pass;
    };
} // namespace Engine


#endif // RENDER_SINGLEPASSMATERIAL_INCLUDED
