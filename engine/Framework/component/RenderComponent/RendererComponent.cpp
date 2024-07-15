#include "RendererComponent.h"
#include "Render/Material.h"

namespace Engine
{
    RendererComponent::RendererComponent(std::shared_ptr<Material> material, std::weak_ptr<GameObject> gameObject) : Component(gameObject) {
        m_material = material;
        // material->RegisterComponent(this);
    }
    void RendererComponent::Tick(float dt)
    {
    }
} // namespace Engine
