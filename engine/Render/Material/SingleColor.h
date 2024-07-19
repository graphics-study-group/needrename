#ifndef RENDER_MATERIAL_SINGLECOLOR_INCLUDED
#define RENDER_MATERIAL_SINGLECOLOR_INCLUDED

#include "Material.h"
#include <glad/glad.h>

namespace Engine {
    class ShaderPass;

    class SingleColor : public Material {
    public:
        SingleColor(std::shared_ptr <RenderSystem> system, GLfloat r, GLfloat g, GLfloat b, GLfloat a);
        ~SingleColor();

        void virtual PrepareDraw(/*...*/) override;

    protected:
        static std::unique_ptr <ShaderPass> pass;
        static GLint location_color;

        GLfloat r, g, b, a;
    };
}

#endif // RENDER_MATERIAL_SINGLECOLOR_INCLUDED
