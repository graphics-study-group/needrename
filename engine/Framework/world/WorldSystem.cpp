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

namespace Engine {
    WorldSystem::WorldSystem() {
        m_main_scene = std::make_unique<Scene>();
    }

    WorldSystem::~WorldSystem() {
    }

    ObjectHandle WorldSystem::NextAvailableObjectHandle() {
        return ObjectHandle{generateGUID(m_go_handle_gen)};
    }

    ComponentHandle WorldSystem::NextAvailableComponentHandle() {
        return ComponentHandle{generateGUID(m_comp_handle_gen)};
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

    Scene &WorldSystem::GetMainSceneRef() noexcept {
        return *m_main_scene;
    }
} // namespace Engine
