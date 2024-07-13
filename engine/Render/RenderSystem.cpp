#include <glad/glad.h>
#include "RenderSystem.h"
#include "Material.h"

namespace Engine
{
    void RenderSystem::render()
    {
        for (auto mat : m_materials) {
            mat->DrawAllComponents(/*Context*/);
        }
    }

    void Engine::RenderSystem::RegisterMaterial(std::shared_ptr <Material> material)
    {
        m_materials.push_back(material);
    }
}
