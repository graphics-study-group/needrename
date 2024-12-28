#include <Framework/object/GameObject.h>
#include "Framework/component/Component.h"
#include "GameObject.h"

namespace Engine
{
    GameObject::GameObject() : 
        m_transformComponent(std::make_shared<TransformComponent>(weak_from_this()))
    {
        //ctor
        this->AddComponent(m_transformComponent);
    }

    GameObject::~GameObject()
    {
        //dtor
    }

    void GameObject::Init()
    {
        for (auto component : m_components)
        {
            component->Init();
        }
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

    const Transform & GameObject::GetTransform() const
    {
        return m_transformComponent->GetTransform();
    }

    Transform & GameObject::GetTransformRef() 
    {
        return m_transformComponent->GetTransformRef();
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

    void GameObject::SetParent(std::shared_ptr<GameObject> parent)
    {
        m_parentGameObject = parent;
        parent->m_childGameObject.push_back(shared_from_this());
    }
}
