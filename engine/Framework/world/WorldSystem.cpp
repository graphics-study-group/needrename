#include "WorldSystem.h"
#include <Asset/Scene/LevelAsset.h>
#include <Asset/Scene/GameObjectAsset.h>

namespace Engine
{
    WorldSystem::WorldSystem()
    {
    }

    WorldSystem::~WorldSystem()
    {
    }

    void WorldSystem::Tick()
    {
        for (auto &comp : m_all_components)
        {
            comp->Tick();
        }
    }

    GUID WorldSystem::GenerateID()
    {
        return generateGUID(m_id_gen);
    }

    void WorldSystem::LoadGameObjectInQueue()
    {
        for (auto &go : m_go_loading_queue)
        {
            for (auto &comp : go->m_components)
                comp->Init();
            m_all_components.insert(m_all_components.end(), go->m_components.begin(), go->m_components.end());
            m_game_objects.push_back(go);
        }
        m_go_loading_queue.clear();
    }

    void WorldSystem::LoadLevelAsset(std::shared_ptr<LevelAsset> levelAsset)
    {
        for (auto &go : levelAsset->m_gameobjects)
        {
            AddGameObjectToWorld(go);
        }
        m_active_camera = levelAsset->m_default_camera;
    }

    void WorldSystem::LoadGameObjectAsset(std::shared_ptr<GameObjectAsset> gameObjectAsset)
    {
        AddGameObjectToWorld(gameObjectAsset->m_MainObject);
    }

    const std::vector<std::shared_ptr<GameObject>> &WorldSystem::GetGameObjects() const
    {
        return m_game_objects;
    }
}
