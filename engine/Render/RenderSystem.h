#ifndef RENDER_RENDERSYSTEM_INCLUDED
#define RENDER_RENDERSYSTEM_INCLUDED

#include <vector>
#include <memory>

namespace Engine
{
    class RendererComponent;

    class RenderSystem
    {
    public:
        // RenderSystem();
        // ~RenderSystem();

        void Render();
        void RegisterComponent(std::shared_ptr <RendererComponent>);
        
    private:
        // TODO: data: mesh, texture, light
        std::vector <std::shared_ptr<RendererComponent>> m_components;
    };
}

#endif // RENDER_RENDERSYSTEM_INCLUDED
