#ifndef RENDER_RENDERSYSTEM_INCLUDED
#define RENDER_RENDERSYSTEM_INCLUDED

#include <vector>
#include <memory>

namespace Engine
{
    class RendererComponent;
    class CameraComponent;

    class RenderSystem
    {
    public:
        // RenderSystem();
        // ~RenderSystem();

        void Render();
        void RegisterComponent(std::shared_ptr <RendererComponent>);
        void SetActiveCamera(std::shared_ptr <CameraComponent>);
        
    private:
        std::vector <std::shared_ptr<RendererComponent>> m_components {};
        std::shared_ptr <CameraComponent> m_active_camera {};
    };
}

#endif // RENDER_RENDERSYSTEM_INCLUDED
