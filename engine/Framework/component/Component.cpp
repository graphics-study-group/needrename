#include "Component.h"
#include <Framework/object/GameObject.h>
#include <Reflection/serialization.h>

namespace Engine {
    Component::Component(GameObject *parent) : m_parentGameObject(parent->GetHandle()), m_scene(parent->m_scene) {
    }

    void Component::Init() {
    }

    void Component::Tick() {
    }

    ComponentHandle Component::GetHandle() const noexcept {
        return m_handle;
    }

    GameObject *Component::GetParentGameObject() const {
        if (!m_parentGameObject.IsValid()) return nullptr;
        return m_scene->GetGameObject(m_parentGameObject);
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
