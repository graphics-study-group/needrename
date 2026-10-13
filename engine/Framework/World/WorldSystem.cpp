#include "WorldSystem.h"
#include "EventQueue.h"
#include <AnnoRefl/Type.h>
#include <Core/Delegate/Delegate.h>
#include <Framework/Component/RenderComponent/CameraComponent.h>
#include <Framework/Component/RenderComponent/LightComponent.h>
#include <Framework/Component/RenderComponent/RendererComponent.h>
#include <Framework/Component/TransformComponent/TransformComponent.h>
#include <Framework/MainClass.h>
#include <Framework/Object/GameObject.h>
#include <Render/Pipeline/Renderer/Camera.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Render/RenderSystem/RendererManager.h>
#include <Render/RenderSystem/SceneDataManager.h>

#include <AnnoRefl/serialization.h>
#include <Asset/AssetRef.h>
#include <Framework/Scene/LevelAsset.h>

#include <algorithm>

namespace Engine {
    WorldSystem::WorldSystem() {
        uint32_t sceneID = m_scene_id_gen++;
        m_scene_map[sceneID] = std::shared_ptr<Scene>(new Scene(sceneID, true, true));
        m_main_scene = m_scene_map[sceneID];
    }

    WorldSystem::~WorldSystem() {
    }

    namespace {
        /// Radiance a light contributes, used to rank shadow-casting lights.
        float LightBrightness(const LightComponent &light) {
            const glm::vec3 radiance = light.m_color * light.m_intensity;
            return std::max(radiance.r, std::max(radiance.g, radiance.b));
        }
    } // namespace

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

        // Only a bounded number of directional lights can hold a shadow map
        // slot. Keep the brightest ones and let the rest light the scene
        // without dynamic shadows.
        std::stable_sort(
            casting_light.begin(), casting_light.end(), [](const LightComponent *lhs, const LightComponent *rhs) {
                return LightBrightness(*lhs) > LightBrightness(*rhs);
            }
        );
        constexpr size_t MAX_CASTING = RenderSystemState::SceneDataManager::MAX_SHADOW_CASTING_LIGHTS;
        for (size_t i = MAX_CASTING; i < casting_light.size(); i++) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Shadow casting directional light '%s' is dropped: at most %zu shadow casting directional lights are "
                "supported. It keeps lighting the scene without shadows.",
                casting_light[i]->GetParentGameObject()->m_name.c_str(),
                MAX_CASTING
            );
            non_casting_light.push_back(casting_light[i]);
        }
        if (casting_light.size() > MAX_CASTING) casting_light.resize(MAX_CASTING);

        constexpr size_t MAX_NON_CASTING = RenderSystemState::SceneDataManager::MAX_NON_SHADOW_CASTING_LIGHTS;
        if (non_casting_light.size() > MAX_NON_CASTING) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "%zu non shadow casting lights are present, but only %zu are supported; the extra ones are ignored.",
                non_casting_light.size(),
                MAX_NON_CASTING
            );
            non_casting_light.resize(MAX_NON_CASTING);
        }

        // The cascades follow the active camera; without one the lights keep
        // their previous fit, so no invalid shadow sampling can occur.
        const auto active_camera = GetActiveCamera();

        const uint32_t casting_count = static_cast<uint32_t>(casting_light.size());
        scene_data_manager.SetLightCount(casting_count);
        for (uint32_t i = 0; i < casting_count; ++i) {
            auto transform = casting_light[i]->GetParentGameObject()->GetWorldTransform();
            const glm::vec3 direction = glm::normalize(transform.GetRotation() * glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 radiance = casting_light[i]->m_color * casting_light[i]->m_intensity;
            if (active_camera) {
                // The shadow distance and the cascade count are clamped inside
                // the scene data manager, next to the fit that consumes them.
                scene_data_manager.SetLightDirectional(
                    i,
                    direction,
                    radiance,
                    active_camera->GetViewMatrix(),
                    active_camera->GetProjectionMatrix(),
                    active_camera->m_clipping_near,
                    active_camera->m_clipping_far,
                    casting_light[i]->m_shadow_distance,
                    casting_light[i]->m_cascade_count
                );
            } else {
                scene_data_manager.SetLightDirectional(i, direction, radiance);
            }
        }
        const uint32_t non_casting_count = static_cast<uint32_t>(non_casting_light.size());
        scene_data_manager.SetLightCountNonShadowCasting(non_casting_count);
        for (uint32_t i = 0; i < non_casting_count; ++i) {
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

    void WorldSystem::UpdateRendererData(RenderSystem &render_system) {
        auto &scene = GetMainSceneRef();

        for (auto &comp : scene.GetComponents()) {
            if (auto *renderer_comp = dynamic_cast<RendererComponent *>(comp.get())) {
                renderer_comp->PreRenderUpdate();
            }
        }

        UpdateLightData(render_system.GetSceneDataManager());
    }

    std::shared_ptr<Camera> WorldSystem::GetActiveCamera() const noexcept {
        auto camera_comp = m_main_scene->GetComponent<CameraComponent>(m_active_camera);
        if (camera_comp) return camera_comp->m_camera;
        return nullptr;
    }

    uint32_t WorldSystem::GetShadowCastingLightCount() const noexcept {
        uint32_t count = 0;
        for (auto &comp : m_main_scene->GetComponents()) {
            auto *light = dynamic_cast<LightComponent *>(comp.get());
            if (light == nullptr) continue;
            if (!light->m_cast_shadow || light->m_type != LightType::Directional) continue;
            count++;
        }
        return std::min<uint32_t>(count, RenderSystemState::SceneDataManager::MAX_SHADOW_CASTING_LIGHTS);
    }
    void WorldSystem::SetActiveCamera(
        ComponentHandle camera_comp, RenderSystemState::CameraManager *registrar
    ) noexcept {
        m_active_camera = camera_comp;
        if (registrar) {
            registrar->RegisterCamera(GetActiveCamera());
        }
    }

    Scene &WorldSystem::GetMainSceneRef() {
        return *m_main_scene;
    }
    Scene &WorldSystem::GetSceneRef(uint32_t sceneID) {
        if (m_scene_map.find(sceneID) == m_scene_map.end()) throw std::runtime_error("Scene not found.");
        return *m_scene_map[sceneID];
    }
    Scene *WorldSystem::GetScenePtr(uint32_t sceneID) {
        if (m_scene_map.find(sceneID) == m_scene_map.end()) return nullptr;
        return m_scene_map[sceneID].get();
    }
    Scene &WorldSystem::CreateScene() {
        uint32_t sceneID = m_scene_id_gen++;
        m_scene_map[sceneID] = std::shared_ptr<Scene>(new Scene(sceneID, false, false));
        return *m_scene_map[sceneID];
    }
    void WorldSystem::ClearUnusedScenes() {
        for (auto it = m_scene_map.begin(); it != m_scene_map.end();) {
            if (it->second.use_count() == 1) {
                it = m_scene_map.erase(it);
            } else {
                ++it;
            }
        }
    }

    void WorldSystem::SaveLevelToAsset(LevelAsset &level_asset) {
        level_asset.m_default_camera = m_active_camera;
        level_asset.m_skybox_material = m_skybox_material;
        level_asset.SaveFromScene(*m_main_scene);
    }
} // namespace Engine
