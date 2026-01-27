#include "Component.h"
#include <Framework/object/GameObject.h>

namespace Engine {
    Component::Component(ObjectHandle parent_object) : m_handle(parent_object) {
    }

    void Component::Init() {
    }

    void Component::Tick() {
    }

    ComponentHandle Component::GetHandle() const noexcept {
        return m_handle;
    }

    GameObject *Component::GetParentGameObject() const {
        if (!m_parentGameObject) return nullptr;
        return WorldSystem::GetInstance().GetGameObject(m_parentGameObject);
    }

    bool Component::operator==(const Component &other) const noexcept {
        return this->m_handle == other.m_handle;
    }
} // namespace Engine
