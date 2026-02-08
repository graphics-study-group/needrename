#include "WorldSystem.h"
#include <Core/Delegate/Delegate.h>
#include <Core/Functional/EventQueue.h>
#include <Framework/component/RenderComponent/LightComponent.h>
#include <Framework/component/RenderComponent/RendererComponent.h>
#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Framework/object/GameObject.h>
#include <MainClass.h>
#include <Reflection/Type.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Render/RenderSystem/RendererManager.h>
#include <Render/RenderSystem/SceneDataManager.h>

#include <Asset/AssetRef.h>
#include <Asset/Scene/LevelAsset.h>
#include <Reflection/serialization.h>

namespace Engine {
    WorldSystem::WorldSystem() {
        auto &scene = CreateScene();
        m_main_scene = m_scene_map[scene.GetID()];
    }

    WorldSystem::~WorldSystem() {
    }

    void WorldSystem::UpdateLightData(RenderSystemState::SceneDataManager &scene_data_manager) {
        std::vector<LightComponent *> casting_light;
        std::vector<LightComponent *> non_casting_light;
        for (auto &comp : m_main_scene->GetComponents()) {
            auto ptr = dynamic_cast<LightComponent *>(comp.get());
            if (ptr) {
                if (ptr->m_cast_shadow) {
                    if (ptr->m_type == LightType::Directional) {
                        casting_light.push_back(ptr);
                    } else {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_APPLICATION, "Shadow casting point light and spot light are not supported."
                        );
                    }
                } else {
                    if (ptr->m_type != LightType::Spot) {
                        non_casting_light.push_back(ptr);
                    } else {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Spot light is not supported.");
                    }
                }
            }
        }
        scene_data_manager.SetLightCount(casting_light.size());
        for (uint32_t i = 0; i < casting_light.size(); ++i) {
            auto transform = casting_light[i]->GetParentGameObject()->GetWorldTransform();
            scene_data_manager.SetLightDirectional(
                i,
                glm::normalize(transform.GetRotation() * glm::vec3(0.0f, 1.0f, 0.0f)),
                casting_light[i]->m_color * casting_light[i]->m_intensity
            );
        }
        scene_data_manager.SetLightCountNonShadowCasting(non_casting_light.size());
        for (uint32_t i = 0; i < non_casting_light.size(); ++i) {
            auto transform = non_casting_light[i]->GetParentGameObject()->GetWorldTransform();
            switch (non_casting_light[i]->m_type) {
            case LightType::Directional:
                scene_data_manager.SetLightDirectionalNonShadowCasting(
                    i,
                    glm::normalize(transform.GetRotation() * glm::vec3(0.0f, 1.0f, 0.0f)),
                    non_casting_light[i]->m_color * non_casting_light[i]->m_intensity
                );
                break;
            case LightType::Point:
                scene_data_manager.SetLightPointNonShadowCasting(
                    i, transform.GetPosition(), non_casting_light[i]->m_color * non_casting_light[i]->m_intensity
                );
                break;
            default:
                break;
            }
        }
    }

    std::shared_ptr<Camera> WorldSystem::GetActiveCamera() const noexcept {
        return m_active_camera;
    }
    void WorldSystem::SetActiveCamera(
        std::shared_ptr<Camera> camera, RenderSystemState::CameraManager *registrar
    ) noexcept {
        m_active_camera = camera;
        if (registrar) {
            registrar->RegisterCamera(camera);
        }
    }

    Scene &WorldSystem::GetMainSceneRef() {
        return *m_main_scene;
    }
    Scene &WorldSystem::GetSceneRef(uint32_t sceneID) {
        if (m_scene_map.find(sceneID) == m_scene_map.end())
            throw std::runtime_error("Scene not found.");
        return *m_scene_map[sceneID];
    }
    Scene *WorldSystem::GetScenePtr(uint32_t sceneID) {
        if (m_scene_map.find(sceneID) == m_scene_map.end())
            return nullptr;
        return m_scene_map[sceneID].get();
    }
    Scene &WorldSystem::CreateScene() {
        uint32_t sceneID = m_scene_id_gen++;
        m_scene_map[sceneID] = std::shared_ptr<Scene>(new Scene(sceneID));
        return *m_scene_map[sceneID];
    }
    void WorldSystem::RemoveScene(uint32_t sceneID) {
        if (m_scene_map.find(sceneID) != m_scene_map.end())
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Scene %d not found.", sceneID);
        m_scene_map.erase(sceneID);
    }

    void WorldSystem::SaveLevelToArchive(Serialization::Archive &archive) {
        auto level_asset = std::make_unique<LevelAsset>(m_main_scene);
        level_asset->m_default_camera = m_active_camera;
        level_asset->m_skybox_material = m_skybox_material;
        archive.clear();
        archive.prepare_save();
        level_asset->save_asset_to_archive(archive);
    }
} // namespace Engine
