#include "GameObject.h"
#include <Framework/component/Component.h>
#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>

namespace Engine {
    // TODO: save WorldSystem reference. Need the reflection parser support no backdoor constructor.
    const Transform &GameObject::GetTransform() const {
        return MainClass::GetInstance()
            ->GetWorldSystem()
            ->GetComponent<TransformComponent>(m_transformComponent)
            ->GetTransform();
    }
    Transform &GameObject::GetTransformRef() {
        return MainClass::GetInstance()
            ->GetWorldSystem()
            ->GetComponent<TransformComponent>(m_transformComponent)
            ->GetTransformRef();
    }

    Transform GameObject::GetWorldTransform() {
        if (m_parentGameObject.IsValid()) {
            return MainClass::GetInstance()->GetWorldSystem()->GetGameObject(m_parentGameObject)->GetWorldTransform()
                   * GetTransform();
        }
        return GetTransform();
    }

    void GameObject::SetTransform(const Transform &transform) {
        MainClass::GetInstance()
            ->GetWorldSystem()
            ->GetComponent<TransformComponent>(m_transformComponent)
            ->SetTransform(transform);
    }

    void GameObject::SetParent(ObjectHandle parent) {
        m_parentGameObject = parent;
        MainClass::GetInstance()->GetWorldSystem()->GetGameObject(parent)->m_childGameObject.push_back(m_handle);
    }

    ObjectHandle GameObject::GetHandle() const noexcept {
        return m_handle;
    }

    bool GameObject::operator==(const GameObject &other) const noexcept {
        return this->m_handle == other.m_handle;
    }
} // namespace Engine
