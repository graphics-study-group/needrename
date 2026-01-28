#include "GameObject.h"
#include <Framework/component/Component.h>
#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <MainClass.h>

namespace Engine {
    GameObject::GameObject(Scene *scene) : m_scene(scene) {
    }

    const Transform &GameObject::GetTransform() const {
        return m_scene->GetComponent<TransformComponent>(m_transformComponent)->GetTransform();
    }
    Transform &GameObject::GetTransformRef() {
        return m_scene->GetComponent<TransformComponent>(m_transformComponent)->GetTransformRef();
    }

    Transform GameObject::GetWorldTransform() {
        if (m_parentGameObject.IsValid()) {
            return m_scene->GetGameObject(m_parentGameObject)->GetWorldTransform() * GetTransform();
        }
        return GetTransform();
    }

    void GameObject::SetTransform(const Transform &transform) {
        m_scene->GetComponent<TransformComponent>(m_transformComponent)->SetTransform(transform);
    }

    void GameObject::SetParent(ObjectHandle parent) {
        m_parentGameObject = parent;
        m_scene->GetGameObject(parent)->m_childGameObject.push_back(m_handle);
    }

    ObjectHandle GameObject::GetHandle() const noexcept {
        return m_handle;
    }

    bool GameObject::operator==(const GameObject &other) const noexcept {
        return this->m_handle == other.m_handle;
    }
} // namespace Engine
