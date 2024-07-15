#ifndef RENDER_MATERIAL_H
#define RENDER_MATERIAL_H

#include <vector>
#include <memory>
#include <GLAD/glad.h>

namespace Engine {
    class RenderSystem;
    class RendererComponent;
    class Material {
    public:
        Material (std::shared_ptr<RenderSystem> system, const char * vertex, const char * fragment);
        ~Material ();

        void PrepareDraw(/*Context, Transform, etc.*/);
    protected:
        GLuint m_shaderProgram;
        std::weak_ptr <RenderSystem> m_renderSystem;
    };
};

#endif // RENDER_MATERIAL_H
