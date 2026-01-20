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
            m_all_components.insert(m_all_components.end(), go->m_components.begin(), go->m_components.end());
            m_game_objects.push_back(go);
            for (auto &comp : go->m_components) {
                auto render_comp = std::dynamic_pointer_cast<RendererComponent>(comp);
                if (render_comp) {
                    render_comp->RenderInit();
                }
                event_queue->AddEvent(comp, &Component::Init);
            }
        }
        m_go_loading_queue.clear();
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
} // namespace Engine
