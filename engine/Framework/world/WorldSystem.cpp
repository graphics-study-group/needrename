#include "WorldSystem.h"
#include <Asset/Scene/GameObjectAsset.h>
#include <Asset/Scene/LevelAsset.h>
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
    }

    WorldSystem::~WorldSystem() {
    }

    WorldSystem &WorldSystem::GetInstance() {
        return *MainClass::GetInstance()->GetWorldSystem();
    }

    ObjectHandle WorldSystem::NextAvailableObjectHandle() {
        return ObjectHandle{generateGUID(m_go_handle_gen)};
    }

    ComponentHandle WorldSystem::NextAvailableComponentHandle() {
        return ComponentHandle{generateGUID(m_comp_handle_gen)};
    }

    GameObject &WorldSystem::CreateGameObject() {
        auto go_ptr = std::unique_ptr<GameObject>(new GameObject());
        auto handle = this->NextAvailableObjectHandle();
        auto &ret = *go_ptr;
        go_ptr->m_handle = handle;
        m_go_map[handle] = go_ptr.get();
        auto transform_component = go_ptr->template AddComponent<TransformComponent>();
        go_ptr->m_transformComponent = transform_component.m_handle;
        m_go_add_queue.push_back(std::move(go_ptr));
        return ret;
    }

    Component &WorldSystem::CreateComponent(ObjectHandle objectHandle, const Reflection::Type &type) {
        auto comp_var = type.CreateInstance(objectHandle);
        return AddComponent(objectHandle, static_cast<Component *>(comp_var.GetDataPtr()));
    }

    Component &WorldSystem::AddComponent(ObjectHandle objectHandle, Component *ptr) {
        auto comp_ptr = std::unique_ptr<Component>(static_cast<Component *>(ptr));
        auto handle = this->NextAvailableComponentHandle();
        auto &ret = *comp_ptr;
        comp_ptr->m_handle = handle;
        m_comp_map[handle] = comp_ptr.get();
        if (auto obj = this->GetGameObject(objectHandle)) {
            obj->m_components.push_back(handle);
        }
        m_comp_add_queue.push_back(std::move(comp_ptr));
        return ret;
    }

    void WorldSystem::RemoveGameObject(ObjectHandle handle) {
        m_go_remove_queue.push_back(handle);
    }

    void WorldSystem::RemoveComponent(ComponentHandle handle) {
        m_comp_remove_queue.push_back(handle);
    }

    void WorldSystem::FlushCmdQueue() {
        auto event_queue = MainClass::GetInstance()->GetEventQueue();

        for (auto &go_ptr : m_go_add_queue) {
            m_game_objects.push_back(std::move(go_ptr));
        }
        m_go_add_queue.clear();
        for (auto handle : m_go_remove_queue) {
            auto go_ptr = this->GetGameObject(handle);
            for (auto comp : go_ptr->m_components) {
                this->RemoveComponent(comp);
            }
            m_go_map.erase(handle);
            m_game_objects.erase(
                std::remove_if(
                    m_game_objects.begin(),
                    m_game_objects.end(),
                    [&handle](const std::unique_ptr<GameObject> &obj) { return obj->GetHandle() == handle; }
                ),
                m_game_objects.end()
            );
        }
        m_go_remove_queue.clear();

        for (auto &comp_ptr : m_comp_add_queue) {
            m_components.push_back(std::move(comp_ptr));
            event_queue->AddEvent(comp_ptr->GetHandle(), &Component::Init);
            // XXX: should not render init here
            auto render_comp = dynamic_cast<RendererComponent *>(comp_ptr.get());
            if (render_comp) {
                render_comp->RenderInit();
            }
        }
        m_comp_add_queue.clear();
        for (auto handle : m_comp_remove_queue) {
            m_comp_map.erase(handle);
            m_components.erase(
                std::remove_if(
                    m_components.begin(),
                    m_components.end(),
                    [&handle](const std::unique_ptr<Component> &comp) { return comp->GetHandle() == handle; }
                ),
                m_components.end()
            );
        }
        m_comp_remove_queue.clear();
    }

    GameObject *WorldSystem::GetGameObject(ObjectHandle handle) {
        auto it = m_go_map.find(handle);
        if (it == m_go_map.end()) {
            return nullptr;
        }
        return it->second;
    }

    Component *WorldSystem::GetComponent(ComponentHandle handle) {
        auto it = m_comp_map.find(handle);
        if (it == m_comp_map.end()) {
            return nullptr;
        }
        return it->second;
    }

    GameObject &WorldSystem::GetGameObjectRef(ObjectHandle handle) {
        auto it = m_go_map.find(handle);
        if (it == m_go_map.end()) {
            throw std::runtime_error("GameObject not found.");
        }
        return *it->second;
    }

    Component &WorldSystem::GetComponentRef(ComponentHandle handle) {
        auto it = m_comp_map.find(handle);
        if (it == m_comp_map.end()) {
            throw std::runtime_error("Component not found.");
        }
        return *it->second;
    }

    const std::vector<std::unique_ptr<GameObject>> &WorldSystem::GetGameObjects() const {
        return m_game_objects;
    }

    const std::vector<std::unique_ptr<Component>> &WorldSystem::GetComponents() const {
        return m_components;
    }

    void WorldSystem::AddInitEvent() {
        auto event_queue = MainClass::GetInstance()->GetEventQueue();
        for (auto &comp : m_components) {
            event_queue->AddEvent(comp->GetHandle(), &Component::Init);
        }
    }

    void WorldSystem::AddTickEvent() {
        auto event_queue = MainClass::GetInstance()->GetEventQueue();
        for (auto &comp : m_components) {
            event_queue->AddEvent(comp->GetHandle(), &Component::Tick);
        }
    }

    void WorldSystem::UpdateLightData(RenderSystemState::SceneDataManager &scene_data_manager) {
        std::vector<LightComponent *> casting_light;
        std::vector<LightComponent *> non_casting_light;
        for (auto &comp : m_components) {
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
} // namespace Engine
