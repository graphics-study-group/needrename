#include "Component.h"
#include <Framework/object/GameObject.h>

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
} // namespace Engine
