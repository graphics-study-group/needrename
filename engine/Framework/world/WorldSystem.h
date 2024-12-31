#ifndef FRAMEWORK_WORLD_WORLDSYSTEM_H
#define FRAMEWORK_WORLD_WORLDSYSTEM_H

#include <random>
#include <vector>
#include <memory>
#include <Core/guid.h>
#include <Framework/object/GameObject.h>
#include <Framework/component/TransformComponent/TransformComponent.h>

namespace Engine
{
    class LevelAsset;
    class GameObjectAsset;

    class WorldSystem
    {
    public:
        WorldSystem();
        ~WorldSystem();

        void Tick(float dt);

        GUID GenerateID();

        template <typename T, typename... Args>
        std::shared_ptr<T> CreateGameObject();
        template <typename T>
        void AddGameObjectToWorld(std::shared_ptr<T> go);
        void LoadLevelAsset(std::shared_ptr<LevelAsset> levelAsset);
        void LoadGameObjectAsset(std::shared_ptr<GameObjectAsset> gameObjectAsset);

    protected:
        std::mt19937_64 m_id_gen{std::random_device{}()};

        std::vector<std::shared_ptr<GameObject>> m_go_loading_queue{};
        std::vector<std::shared_ptr<GameObject>> m_game_objects{};
        std::vector<std::shared_ptr<Component>> m_all_components{};
    };

    template <typename T, typename... Args>
    std::shared_ptr<T> WorldSystem::CreateGameObject()
    {
        static_assert(std::is_base_of<GameObject, T>::value, "T must be derived from GameObject");
        auto game_object = std::make_shared<T>(this);
        game_object->template AddComponent<TransformComponent>();
        game_object->m_transformComponent = dynamic_pointer_cast<TransformComponent>(game_object->m_components[0]);
        return game_object;
    }

    template <typename T>
    void WorldSystem::AddGameObjectToWorld(std::shared_ptr<T> go)
    {
        static_assert(std::is_base_of<GameObject, T>::value, "T must be derived from GameObject");
        m_go_loading_queue.push_back(go);
    }
}
#endif // FRAMEWORK_WORLD_WORLDSYSTEM_H
