#include "LevelAsset.h"

// Load to World use
#include <Framework/world/HandleResolver.h>
#include <Framework/world/Scene.h>
#include <Framework/world/WorldSystem.h>
#include <MainClass.h>
#include <Reflection/serialization.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <Render/Resource/RenderResourceManager.h>

namespace Engine {
    void LevelAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        SceneAsset::save_asset_to_archive(archive);
        auto &json = *archive.m_cursor;
        json["LevelAsset::m_default_camera"] = m_default_camera.GetID();
    }
    void LevelAsset::load_asset_from_archive(Serialization::Archive &archive) {
        SceneAsset::load_asset_from_archive(archive);
        auto &json = *archive.m_cursor;
        if (json.contains("LevelAsset::m_default_camera")) {
            m_default_camera = {json["LevelAsset::m_default_camera"].get<uint32_t>()};
        }
    }

    void LevelAsset::LoadToWorld() {
        auto world = MainClass::GetInstance()->GetWorldSystem();
        auto rsys = MainClass::GetInstance()->GetRenderSystem();

        world->GetMainSceneRef().Clear();
        this->AddToScene(world->GetMainSceneRef());
        if (m_skybox_material.IsValid()) {
            auto material_handle =
                rsys->GetRenderResourceManager().AcquireMaterialInstance(m_skybox_material.GetGUID());
            rsys->GetSceneDataManager().SetSkyboxMaterial(material_handle);
            world->m_skybox_material = m_skybox_material;
        }

        if (m_default_camera.IsValid()) {
            m_archive->prepare_load();
            auto &resolver = m_archive->GetOrCreateResolver<HandleResolver>();
            world->SetActiveCamera(resolver.m_comp_map[m_default_camera.GetID()]);
        }
        auto active_camera = world->GetActiveCamera();
        if (active_camera) rsys->GetCameraManager().RegisterCamera(active_camera);
    }
} // namespace Engine
