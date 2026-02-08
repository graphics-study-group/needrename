#include "Handle.h"
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Reflection/serialization.h>

namespace Engine {
    namespace detail {
        HandleBase::HandleBase(uint32_t sceneID, uint32_t ID) : m_sceneID(sceneID), m_ID(ID) {
        }

        uint32_t HandleBase::GetSceneID() const noexcept {
            return m_sceneID;
        }

        uint32_t HandleBase::GetID() const noexcept {
            return m_ID;
        }

        bool HandleBase::IsValid() const noexcept {
            return m_ID != 0; // not zero
        }

        void HandleBase::Reset() noexcept {
            m_ID = 0; // set zero
        }

        bool HandleBase::operator==(const HandleBase &other) const noexcept {
            return m_ID == other.m_ID;
        }

        void HandleBase::save_to_archive(Engine::Serialization::Archive &archive) const {
            Engine::Serialization::Json &json = *archive.m_cursor;
            json = m_ID;
        }

        void HandleBase::load_from_archive(Engine::Serialization::Archive &archive) {
            Engine::Serialization::Json &json = *archive.m_cursor;
            m_ID = json.get<uint32_t>();
        }
    } // namespace detail

    ObjectHandle::ObjectHandle(uint32_t sceneID, uint32_t ID) : detail::HandleBase(sceneID, ID) {
    }

    GameObject *ObjectHandle::GetGameObject() const {
        auto scene = MainClass::GetInstance()->GetWorldSystem()->GetScenePtr(m_sceneID);
        if (scene) {
            return scene->GetGameObject(*this);
        }
        return nullptr;
    }
    ComponentHandle::ComponentHandle(uint32_t sceneID, uint32_t ID) : detail::HandleBase(sceneID, ID) {
    }
    Component *ComponentHandle::GetComponent() const {
        auto scene = MainClass::GetInstance()->GetWorldSystem()->GetScenePtr(m_sceneID);
        if (scene) {
            return scene->GetComponent(*this);
        }
        return nullptr;
    }
} // namespace Engine
