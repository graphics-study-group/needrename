#include "GameObject.h"
#include <Framework/component/Component.h>
#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <MainClass.h>

#include <algorithm>

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
        GameObject *new_parent_go = nullptr;
        if (parent.IsValid()) {
            new_parent_go = m_scene->GetGameObject(parent);
            if (new_parent_go == nullptr) {
                return;
            }
        }

        // Detach from old parent first to keep parent/children relationship consistent.
        if (m_parentGameObject.IsValid()) {
            if (GameObject *old_parent_go = m_scene->GetGameObject(m_parentGameObject)) {
                auto &old_children = old_parent_go->m_childGameObject;
                old_children.erase(std::remove(old_children.begin(), old_children.end(), m_handle), old_children.end());
            }
        }

        m_parentGameObject = parent;
        if (new_parent_go != nullptr) {
            // Avoid duplicate child handles when setting the same parent repeatedly.
            auto &new_children = new_parent_go->m_childGameObject;
            if (std::find(new_children.begin(), new_children.end(), m_handle) == new_children.end()) {
                new_children.push_back(m_handle);
            }
        }
    }

    ObjectHandle GameObject::GetParent() const noexcept {
        return m_parentGameObject;
    }

    const std::vector<ObjectHandle> &GameObject::GetChildren() const noexcept {
        return m_childGameObject;
    }

    ObjectHandle GameObject::GetHandle() const noexcept {
        return m_handle;
    }

    Scene *GameObject::GetScene() const noexcept {
        return m_scene;
    }

    bool GameObject::operator==(const GameObject &other) const noexcept {
        return this->m_handle == other.m_handle;
    }

    void GameObject::save_to_archive(Serialization::Archive &archive) const {
        Serialization::Json &json = *archive.m_cursor;
        Serialization::Archive temp_archive(archive, &json["GameObject::m_handle"]);
        Serialization::serialize(m_handle, temp_archive);
        this->_SERIALIZATION_SAVE_(archive);
    }

    void GameObject::load_from_archive(Serialization::Archive &archive) {
        Serialization::Json &json = *archive.m_cursor;
        Serialization::Archive temp_archive(archive, &json["GameObject::m_handle"]);
        Serialization::deserialize(m_handle, temp_archive);
        this->_SERIALIZATION_LOAD_(archive);
    }

    Component &GameObject::AddComponent(Component *comp_ptr) {
        m_scene->AddComponent(*this, comp_ptr);
        return *comp_ptr;
    }
} // namespace Engine

#include "__generated__/GameObject.h.inc"
