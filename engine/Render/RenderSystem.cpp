#include "RenderSystem.h"
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include "Framework/component/RenderComponent/RendererComponent.h"
#include "Framework/component/RenderComponent/CameraComponent.h"

namespace Engine
{
    void RenderSystem::Render()
    {
        CameraContext cameraContext{};
        if (m_active_camera) {
            cameraContext = m_active_camera->CreateContext();
        } else {
            SDL_LogWarn(0, "No active camera.");
            cameraContext.projection_matrix = glm::identity<glm::mat4>();
            cameraContext.view_matrix = glm::identity<glm::mat4>();
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        for (auto comp : m_components) {
            comp->Draw(cameraContext);
        }
    }

    void RenderSystem::RegisterComponent(std::shared_ptr<RendererComponent> comp)
    {
        m_components.push_back(comp);
    }

    void RenderSystem::SetActiveCamera(std::shared_ptr <CameraComponent> cameraComponent)
    {
        m_active_camera = cameraComponent;
    }
}
