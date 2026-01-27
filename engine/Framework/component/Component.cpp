#include "Component.h"

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
    
    bool Component::operator==(const Component &other) const noexcept {
        return this->m_handle == other.m_handle;
    }
} // namespace Engine
