#include "Framework/component/TransformComponent/TransformComponent.h"
#include "Framework/go/GameObject.h"
#include "TransformComponent.h"

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

    void TransformComponent::Tick(float dt)
    {
    }

    void TransformComponent::SetTransform(const Transform &transform)
    {
        m_transform = transform;
    }

    const Transform &TransformComponent::GetTransform() const
    {
        return m_transform;
    }

    Transform &TransformComponent::GetTransform()
    {
        return m_transform;
    }

    Transform TransformComponent::GetWorldTransform() const
    {
        auto parentGameObject = m_parentGameObject.lock();
        return parentGameObject->GetWorldTransform() * m_transform;
    }
}
