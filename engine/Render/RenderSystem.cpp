#include "RenderSystem.h"
#include <glad/glad.h>
#include "Framework/component/RenderComponent/RendererComponent.h"

namespace Engine
{
    void RenderSystem::Render()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        for (auto comp : m_components) {
            comp->Draw(/*Context*/);
        }
    }

    void RenderSystem::RegisterComponent(std::shared_ptr<RendererComponent> comp)
    {
        m_components.push_back(comp);
    }
}
