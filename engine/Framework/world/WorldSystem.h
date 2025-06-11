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

        /// @brief Generate a GUID using the random generator in the WorldSystem.
        GUID GenerateID();

        /// @brief GameObject factory function. Create a GameObject and return a shared pointer to it.
        /// @tparam T T must be derived from GameObject
        /// @tparam ...Args The arguments to be passed to the constructor of T
        /// @return The shared pointer to the created GameObject
        template <typename T, typename... Args>
        std::shared_ptr<T> CreateGameObject(Args... args);

        /// @brief Add a GameObject to the loading queue.
        template <typename T>
        void AddGameObjectToWorld(std::shared_ptr<T> go);

        /// @brief Load all GameObjects in the loading queue. This will call Init() on all components of the GameObjects.
        void LoadGameObjectInQueue();

        /// @brief Load a level asset. Add all GameObjects in the level asset to the loading queue.
        void LoadLevelAsset(std::shared_ptr<LevelAsset> levelAsset);

        /// @brief Load a GameObject asset. Add the GameObject in the asset to the loading queue.
        void LoadGameObjectAsset(std::shared_ptr<GameObjectAsset> gameObjectAsset);

    protected:
        std::mt19937_64 m_id_gen{std::random_device{}()};

        std::vector<std::shared_ptr<GameObject>> m_go_loading_queue{};
        std::vector<std::shared_ptr<GameObject>> m_game_objects{};
        std::vector<std::shared_ptr<Component>> m_all_components{};
    };

    template <typename T, typename... Args>
    std::shared_ptr<T> WorldSystem::CreateGameObject(Args... args)
    {
        static_assert(std::is_base_of<GameObject, T>::value, "T must be derived from GameObject");
        GameObject *go_ptr = new T(std::forward<Args>(args)...);
        auto game_object = std::shared_ptr<T>(go_ptr);
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
