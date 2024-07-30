#include "Framework/component/TransformComponent/TransformComponent.h"
#include "Framework/go/GameObject.h"

namespace Engine
{
    TransformComponent::TransformComponent(
        std::weak_ptr<GameObject> gameObject
    ) : Component(gameObject)
    {
    }

    TransformComponent::~TransformComponent()
    {
    }

    Transform TransformComponent::GetWorldTransform() const
    {
        auto parentGameObject = m_parentGameObject.lock();
        return parentGameObject->GetWorldTransform() * m_transform;
    }
}