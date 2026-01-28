#include "SceneAsset.h"
#include <Framework/component/Component.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Reflection/Archive.h>
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
    }

} // namespace Engine
