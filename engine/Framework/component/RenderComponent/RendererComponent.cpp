#include "RendererComponent.h"
#include "Render/Material/Material.h"

namespace Engine
{
    RendererComponent::RendererComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject) {
        // material->RegisterComponent(this);
    }
    void RendererComponent::Tick(float dt)
    {
    }
} // namespace Engine
