#include "Component.h"
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Reflection/serialization.h>

namespace Engine {
    Component::Component(const GameObject &parent) :
        m_parentGameObject(parent.GetHandle()), m_scene(parent.GetScene()) {
    }

    void Component::Awake() {
    }

    void Component::Init() {
    }

    void Component::Tick() {
    }

    ComponentHandle Component::GetHandle() const noexcept {
        return m_handle;
    }

    GameObject *Component::GetParentGameObject() const noexcept {
        if (!m_parentGameObject.IsValid()) return nullptr;
        return m_scene->GetGameObject(m_parentGameObject);
    }

    Scene *Component::GetScene() const noexcept {
        return m_scene;
    }

    bool Component::operator==(const Component &other) const noexcept {
        return this->m_handle == other.m_handle;
    }

    void Component::save_to_archive(Serialization::Archive &archive) const {
        Serialization::Json &json = *archive.m_cursor;
        Serialization::Archive temp_archive(archive, &json["Component::m_handle"]);
        Serialization::serialize(m_handle, temp_archive);
        this->_SERIALIZATION_SAVE_(archive);
    }

    void Component::load_from_archive(Serialization::Archive &archive) {
        Serialization::Json &json = *archive.m_cursor;
        Serialization::Archive temp_archive(archive, &json["Component::m_handle"]);
        Serialization::deserialize(m_handle, temp_archive);
        this->_SERIALIZATION_LOAD_(archive);
    }
} // namespace Engine

#include "__generated__/Component.h.inc"
