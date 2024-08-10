#include "Framework/go/GameObject.h"
#include "Framework/component/Component.h"
#include "GameObject.h"

namespace Engine
{
    GameObject::GameObject() : 
        m_parentGameObject(), 
        m_transformComponent(std::make_shared<TransformComponent>(weak_from_this()))
    {
        //ctor
        // TODO: Implement a way to generate unique IDs for GameObjects
        this->AddComponent(m_transformComponent);
    }

    GameObject::~GameObject()
    {
        //dtor
    }

    void GameObject::Load()
    {
        for (auto component : m_components)
        {
            component->Load();
        }
    }

    void GameObject::Unload()
    {
        for (auto component : m_components)
        {
            component->Unload();
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
}
