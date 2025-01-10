#include "Component.h"

namespace Engine
{
    Component::Component(std::weak_ptr<GameObject> gameObject)
        : m_parentGameObject(gameObject)
    {
    }

    void Component::Init()
    {
    }

    void Component::Tick(float)
    {
    }
}
