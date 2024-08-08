#ifndef RENDER_MATERIAL_SINGLECOLOR_INCLUDED
#define RENDER_MATERIAL_SINGLECOLOR_INCLUDED

#include "Material.h"
#include <glad/glad.h>

namespace Engine {
    class ShaderPass;

    class SingleColor : public Material {
    public:
        SingleColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
        ~SingleColor();

        virtual void Load() override;
        virtual void Unload() override;
        virtual void PrepareDraw(const CameraContext & CameraContext, const RendererContext & RendererContext) override;

    protected:
        static std::unique_ptr <ShaderPass> pass;
        static GLint location_color;

        GLfloat r, g, b, a;
    };
}

#endif // RENDER_MATERIAL_SINGLECOLOR_INCLUDED
