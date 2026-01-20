#include "WorldSystem.h"
#include <Asset/Scene/GameObjectAsset.h>
#include <Asset/Scene/LevelAsset.h>
#include <Core/Delegate/Delegate.h>
#include <Framework/component/RenderComponent/RendererComponent.h>
#include <Render/RenderSystem/CameraManager.h>
#include <Core/Functional/EventQueue.h>
#include <MainClass.h>

namespace Engine {
    WorldSystem::WorldSystem() {
    }

    WorldSystem::~WorldSystem() {
    }

    void WorldSystem::AddInitEvent() {
        auto event_queue = MainClass::GetInstance()->GetEventQueue();
        for (auto &comp : m_all_components) {
            event_queue->AddEvent(comp, &Component::Init);
        }
    }

    void WorldSystem::AddTickEvent() {
        auto event_queue = MainClass::GetInstance()->GetEventQueue();
        for (auto &comp : m_all_components) {
            event_queue->AddEvent(comp, &Component::Tick);
        }
    }

    void WorldSystem::LoadGameObjectInQueue() {
        auto event_queue = MainClass::GetInstance()->GetEventQueue();
        for (auto &go : m_go_loading_queue) {
            go->m_id = m_go_id_counter++;
            m_game_objects.push_back(go);
            for (auto &comp : go->m_components) {
                AddComponent(comp);
                auto render_comp = std::dynamic_pointer_cast<RendererComponent>(comp);
                if (render_comp) {
                    render_comp->RenderInit();
                }
                event_queue->AddEvent(comp, &Component::Init);
            }
        }
        m_go_loading_queue.clear();
    }

    void WorldSystem::RemoveGameObjectFromWorld(std::shared_ptr<GameObject> go) {
        // TODO: Better removal process instead of simple erase.
        auto it = std::find(m_game_objects.begin(), m_game_objects.end(), go);
        if (it != m_game_objects.end()) {
            for (auto &comp : go->m_components) {
                RemoveComponent(comp);
            }
            m_game_objects.erase(it);
        }
    }

    void WorldSystem::RefreshGameObjectInWorld(std::shared_ptr<GameObject> go) {
        auto it = std::find(m_game_objects.begin(), m_game_objects.end(), go);
        if (it != m_game_objects.end()) {
            for (auto &comp : go->m_components) {
                RemoveComponent(comp);
            }
            for (auto &comp : go->m_components) {
                AddComponent(comp);
            }
        }
    }

    void WorldSystem::LoadLevelAsset(std::shared_ptr<LevelAsset> levelAsset) {
        for (auto &go : levelAsset->m_gameobjects) {
            AddGameObjectToWorld(go);
        }
        // We cannot register camera here as render system is not guaranteed to be initalized correctly at this point.
        m_active_camera = levelAsset->m_default_camera;
    }

    void WorldSystem::LoadGameObjectAsset(std::shared_ptr<GameObjectAsset> gameObjectAsset) {
        AddGameObjectToWorld(gameObjectAsset->m_MainObject);
    }

    const std::vector<std::shared_ptr<GameObject>> &WorldSystem::GetGameObjects() const {
        return m_game_objects;
    }
    std::shared_ptr<Camera> WorldSystem::GetActiveCamera() const noexcept {
        return m_active_camera;
    }
    void WorldSystem::SetActiveCamera(std::shared_ptr<Camera> camera, RenderSystemState::CameraManager * registrar) noexcept {
        m_active_camera = camera;
        if (registrar) {
            registrar->RegisterCamera(camera);
        }
    }

    void WorldSystem::AddComponent(std::shared_ptr<Component> comp) {
        comp->m_id = m_component_id_counter++;
        m_all_components.push_back(comp);
    }

    void WorldSystem::RemoveComponent(std::shared_ptr<Component> comp) {
        // TODO: Better removal process instead of simple erase.
        auto it = std::find(m_all_components.begin(), m_all_components.end(), comp);
        if (it != m_all_components.end()) {
            m_all_components.erase(it);
        }
    }
} // namespace Engine
