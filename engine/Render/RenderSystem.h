#ifndef RENDERSYSTEM_H
#define RENDERSYSTEM_H

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

        void render();
        void RegisterComponent(std::shared_ptr <RendererComponent>);
        
    private:
        // TODO: data: mesh, texture, light
        std::vector <std::shared_ptr<RendererComponent>> m_components;
    };
}

#endif // RENDERSYSTEM_H
