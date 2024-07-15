#ifndef RENDER_SINGLEPASSMATERIAL_INCLUDED
#define RENDER_SINGLEPASSMATERIAL_INCLUDED

#include "Material.h"
#include <glad/glad.h>

namespace Engine
{
    class SinglePassMaterial : public Material {
    public:
        SinglePassMaterial (std::shared_ptr<RenderSystem> system, const char * vertex, const char * fragment);
        virtual ~SinglePassMaterial();
        void virtual PrepareDraw(/*...*/) override;
    protected:
        GLuint m_shaderProgram;
    };
} // namespace Engine


#endif // RENDER_SINGLEPASSMATERIAL_INCLUDED
