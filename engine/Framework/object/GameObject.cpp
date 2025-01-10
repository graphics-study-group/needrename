#include "GameObject.h"
#include <Framework/object/GameObject.h>
#include <Framework/component/Component.h>
#include <MainClass.h>

namespace Engine
{
    GameObject::GameObject(const WorldSystem *)
    {
        // assert(marker == MainClass::GetWorldSystem().get());
    }

    GameObject::~GameObject()
    {
        // dtor
    }

    void GameObject::AddComponent(std::shared_ptr<Component> component)
    {
        m_components.push_back(component);
        component->m_parentGameObject = weak_from_this();
    }

    const Transform &GameObject::GetTransform() const
    {
        return m_transformComponent->GetTransform();
    }

    Transform &GameObject::GetTransformRef()
    {
        return m_transformComponent->GetTransformRef();
    }

    Transform GameObject::GetWorldTransform()
    {
        auto parentGameObject = m_parentGameObject.lock();
        if (parentGameObject)
            return parentGameObject->GetWorldTransform() * m_transformComponent->GetTransform();
        return m_transformComponent->GetTransform();
    }

    void GameObject::SetTransform(const Transform &transform)
    {
        m_transformComponent->SetTransform(transform);
    }

    void GameObject::SetParent(std::shared_ptr<GameObject> parent)
    {
        m_parentGameObject = parent;
        parent->m_childGameObject.push_back(shared_from_this());
    }
}
