#include "Component.h"

namespace Engine {
    Component::Component(std::weak_ptr<GameObject> gameObject) : m_parentGameObject(gameObject) {
    }

    void Component::Init() {
    }

    void Component::Tick() {
    }

    ObjectID Component::GetID() const noexcept {
        return m_id;
    }
    
    bool Component::operator==(const Component &other) const noexcept {
        return this->m_id == other.m_id;
    }
} // namespace Engine
