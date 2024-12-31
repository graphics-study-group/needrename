#include "Framework/component/TransformComponent/TransformComponent.h"
#include <Framework/object/GameObject.h>
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

    void TransformComponent::Tick(float)
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

    Transform &TransformComponent::GetTransformRef()
    {
        return m_transform;
    }
}
