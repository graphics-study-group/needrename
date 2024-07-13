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
        void DrawAllComponents(/* RenderingContext */);
        void RegisterComponent (std::shared_ptr <RendererComponent> component);
    protected:

        GLuint m_shaderProgram;
        std::vector <RendererComponent *> m_components;
        std::weak_ptr <RenderSystem> m_renderSystem;
    };
};

#endif // RENDER_MATERIAL_H
