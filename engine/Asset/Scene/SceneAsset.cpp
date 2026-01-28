#include "SceneAsset.h"
#include <Framework/component/Component.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Reflection/Archive.h>
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>

namespace Engine {
    SceneAsset::SceneAsset(std::unique_ptr<Scene> &&scene) : m_scene(std::move(scene)) {
        m_scene->FlushCmdQueue();
    }

    void SceneAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        Serialization::Json &json = *archive.m_cursor;
        json["objects"] = Serialization::Json::array();
        for (auto &go : m_scene->GetGameObjects()) {
            Serialization::Json &obj = json["objects"].emplace_back(Serialization::Json::object());
            Serialization::Archive temp_archive(archive, &obj);
            Serialization::serialize(*go, temp_archive);
        }
        json["components"] = Serialization::Json::array();
        for (auto &comp : m_scene->GetComponents()) {
            Serialization::Json &comp_obj = json["components"].emplace_back(Serialization::Json::object());
            Serialization::Archive temp_archive(archive, &comp_obj);
            Serialization::serialize(*comp, temp_archive);
        }

        Asset::save_asset_to_archive(archive);
    }
    void SceneAsset::load_asset_from_archive(Serialization::Archive &archive) {
        Asset::load_asset_from_archive(archive);
        Serialization::Json &json = *archive.m_cursor;
        if (!m_scene) {
            m_scene = std::make_unique<Scene>();
        }
        m_scene->Clear();
        for (auto &js : json["objects"]) {
            Serialization::Archive temp_archive(archive, &js);
            auto &go = *m_scene->m_game_objects.emplace_back(
                std::unique_ptr<GameObject>(new GameObject(Serialization::SerializationMarker{}))
            );
            go.m_scene = m_scene.get();
            Serialization::deserialize(go, temp_archive);
            m_scene->m_go_map[go.GetHandle()] = &go;
        }
        for (auto &js : json["components"]) {
            Serialization::Archive temp_archive(archive, &js);
            auto type = Reflection::GetType(js["%type"].get<std::string>());
            assert(type);
            auto comp_var = type->CreateInstance(Serialization::SerializationMarker{});
            comp_var.MarkNeedFree(false);
            auto &comp = *m_scene->m_components.emplace_back(
                std::unique_ptr<Component>(static_cast<Component *>(comp_var.GetDataPtr()))
            );
            comp.m_scene = m_scene.get();
            Serialization::deserialize(comp, temp_archive);
            m_scene->m_comp_map[comp.GetHandle()] = &comp;
        }
    }

} // namespace Engine
