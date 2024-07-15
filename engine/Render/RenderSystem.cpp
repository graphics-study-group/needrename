#include "RenderSystem.h"
#include <glad/glad.h>
#include "Framework/component/RenderComponent/RendererComponent.h"

namespace Engine
{
    void RenderSystem::render()
    {
        for (auto comp : m_components) {
            comp->draw(/*Context*/);
        }
    }

    void RenderSystem::RegisterComponent(std::shared_ptr<RendererComponent> comp)
    {
        m_components.push_back(comp);
    }
}
