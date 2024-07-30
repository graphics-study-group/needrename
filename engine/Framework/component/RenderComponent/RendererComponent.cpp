#include "RendererComponent.h"
#include "Render/Material/Material.h"
#include "Framework/go/GameObject.h"

namespace Engine
{
    RendererComponent::RendererComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject) {
        // material->RegisterComponent(this);
    }

    Transform RendererComponent::GetWorldTransform() const
    {
        auto parentGameObject = m_parentGameObject.lock();
        assert(parentGameObject && "A renderer component has no parent game object.");
        return parentGameObject->GetWorldTransform();
    }

    void RendererComponent::Tick(float dt)
    {
    }
} // namespace Engine
