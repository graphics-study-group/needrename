#include "Framework/go/GameObject.h"
#include "Framework/component/Component.h"

namespace Engine
{
    GameObject::GameObject()
    {
        //ctor
        // TODO: Implement a way to generate unique IDs for GameObjects
    }

    GameObject::~GameObject()
    {
        //dtor
    }

    void GameObject::tick(float dt)
    {
        for (auto component : m_components)
        {
            component->tick(dt);
        }
    }

    void GameObject::AddComponent(std::shared_ptr<Component> component)
    {
        m_components.push_back(component);
    }
}