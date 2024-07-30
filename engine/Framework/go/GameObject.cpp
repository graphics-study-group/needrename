#include "Framework/go/GameObject.h"
#include "Framework/component/Component.h"

namespace Engine
{
    GameObject::GameObject()
    {
        //ctor
        // TODO: Implement a way to generate unique IDs for GameObjects
        m_parentGameObject = std::weak_ptr<GameObject>();
        m_transformComponent = std::make_shared<TransformComponent>(weak_from_this());
        this->AddComponent(m_transformComponent);
    }

    GameObject::~GameObject()
    {
        //dtor
    }

    void GameObject::Tick(float dt)
    {
        for (auto component : m_components)
        {
            component->Tick(dt);
        }
    }

    void GameObject::AddComponent(std::shared_ptr<Component> component)
    {
        m_components.push_back(component);
    }

    Transform GameObject::GetTransform()
    {
        return m_transformComponent->GetTransform();
    }

    Transform GameObject::GetWorldTransform()
    {
        auto parentGameObject = m_parentGameObject.lock();
        if(parentGameObject)
            return parentGameObject->GetWorldTransform() * m_transformComponent->GetTransform();
        return m_transformComponent->GetTransform();
    }

    void GameObject::SetTransform(const Transform& transform)
    {
        m_transformComponent->SetTransform(transform);
    }
}
