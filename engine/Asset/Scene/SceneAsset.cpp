#include "SceneAsset.h"
#include <Framework/component/Component.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Handle.h>
#include <Framework/world/HandleResolver.h>
#include <Framework/world/Scene.h>
#include <Reflection/Archive.h>
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <unordered_map>

namespace Engine {
    SceneAsset::~SceneAsset() {
    }

    void SceneAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        if (m_archive) {
            archive.m_context->json = m_archive->m_context->json;
            archive.m_context->extra_data = m_archive->m_context->extra_data;
        }
        archive.m_cursor = &archive.m_context->json["%main_data"];
        Asset::save_asset_to_archive(archive);
    }
    void SceneAsset::load_asset_from_archive(Serialization::Archive &archive) {
        m_archive = std::make_unique<Serialization::Archive>();
        m_archive->m_context->json = archive.m_context->json;
        m_archive->m_context->extra_data = archive.m_context->extra_data;
        Asset::load_asset_from_archive(archive);
    }

    void SceneAsset::SaveFromScene(const Scene &scene) {
        m_archive = std::make_unique<Serialization::Archive>();
        m_archive->prepare_save();
        auto &json = *m_archive->m_cursor;
        json["SceneAsset::objects"] = Serialization::Json::array();
        for (auto &object : scene.GetGameObjects()) {
            json["SceneAsset::objects"].push_back(Serialization::Json::object());
            auto &object_json = json["SceneAsset::objects"].back();
            Serialization::Archive temp(*m_archive, &object_json);
            Serialization::serialize(*object, temp);
        }
        json["SceneAsset::components"] = Serialization::Json::array();
        for (auto &component : scene.GetComponents()) {
            json["SceneAsset::components"].push_back(Serialization::Json::object());
            auto &component_json = json["SceneAsset::components"].back();
            Serialization::Archive temp(*m_archive, &component_json);
            Serialization::serialize(*component, temp);
        }
    }

    void SceneAsset::AddToScene(Scene &scene) {
        if (!m_archive) {
            return;
        }
        m_archive->prepare_load();
        auto &json = *m_archive->m_cursor;
        auto &resolver = m_archive->GetOrCreateResolver<HandleResolver>();
        // Add zero handle
        resolver.m_obj_map[0] = scene.GetNullObjectHandle();
        resolver.m_comp_map[0] = scene.GetNullComponentHandle();
        // Create GO and Comps and setup HandleResolver
        for (auto &object_json : json["SceneAsset::objects"]) {
            auto &go = scene.CreateGameObject();
            resolver.m_obj_map[object_json["GameObject::m_handle"].get<uint32_t>()] = go.GetHandle();
        }
        for (auto &component_json : json["SceneAsset::components"]) {
            auto &parent_go =
                scene.GetGameObjectRef(resolver.m_obj_map[component_json["Component::m_parentGameObject"].get<uint32_t>()]);
            auto type = Reflection::GetType(component_json["%type"].get<std::string>());
            auto &comp = scene.CreateComponent(parent_go, *type);
            resolver.m_comp_map[component_json["Component::m_handle"].get<uint32_t>()] = comp.GetHandle();
        }
        // Deserialize GO and Comps
        for (auto &object_json : json["SceneAsset::objects"]) {
            auto &go = scene.GetGameObjectRef(resolver.m_obj_map[object_json["GameObject::m_handle"].get<uint32_t>()]);
            Serialization::Archive temp(*m_archive, &object_json);
            Serialization::deserialize(go, temp);
        }
        for (auto &component_json : json["SceneAsset::components"]) {
            auto &comp =
                scene.GetComponentRef(resolver.m_comp_map[component_json["Component::m_handle"].get<uint32_t>()]);
            Serialization::Archive temp(*m_archive, &component_json);
            Serialization::deserialize(comp, temp);
        }
    }
} // namespace Engine
