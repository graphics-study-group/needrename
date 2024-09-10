#include "Component.h"

namespace Engine
{
    Component::Component(std::weak_ptr<GameObject> gameObject)
        : m_parentGameObject(gameObject)
    {
    }

    Component::~Component()
    {
    }

    void Component::Load()
    {
    }

    void Component::Unload()
    {
    }
}
