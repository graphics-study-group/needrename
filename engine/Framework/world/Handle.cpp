#include "Handle.h"
#include "HandleResolver.h"
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Reflection/serialization.h>

namespace Engine {
    namespace detail {
        HandleBase::HandleBase(uint32_t ID) : m_sceneID(0), m_ID(ID) {
        }

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
    } // namespace detail

    ObjectHandle::ObjectHandle(uint32_t ID) : detail::HandleBase(ID) {
    }
    ObjectHandle::ObjectHandle(uint32_t sceneID, uint32_t ID) : detail::HandleBase(sceneID, ID) {
    }
    GameObject *ObjectHandle::GetGameObject() const {
        auto scene = MainClass::GetInstance()->GetWorldSystem()->GetScenePtr(m_sceneID);
        if (scene) {
            return scene->GetGameObject(*this);
        }
        return nullptr;
    }
    void ObjectHandle::load_from_archive(Engine::Serialization::Archive &archive) {
        Engine::Serialization::Json &json = *archive.m_cursor;
        auto &resolver = archive.GetOrCreateResolver<HandleResolver>();
        assert(resolver.m_obj_map.find(json.get<uint32_t>()) != resolver.m_obj_map.end());
        *this = resolver.m_obj_map[json.get<uint32_t>()];
    }

    ComponentHandle::ComponentHandle(uint32_t ID) : detail::HandleBase(ID) {
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

    void ComponentHandle::load_from_archive(Engine::Serialization::Archive &archive) {
        Engine::Serialization::Json &json = *archive.m_cursor;
        auto &resolver = archive.GetOrCreateResolver<HandleResolver>();
        assert(resolver.m_comp_map.find(json.get<uint32_t>()) != resolver.m_comp_map.end());
        *this = resolver.m_comp_map[json.get<uint32_t>()];
    }
} // namespace Engine
