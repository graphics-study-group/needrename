#include "RendererComponent.h"
#include "Render/Material/Material.h"
#include "Framework/component/RenderComponent/CameraComponent.h"
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

    RendererContext RendererComponent::CreateContext() const
    {
        return RendererContext{ this->GetWorldTransform().GetTransformMatrix() };
    }

    void RendererComponent::Tick(float dt)
    {
    }
} // namespace Engine
