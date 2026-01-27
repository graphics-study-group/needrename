#ifndef WORLD_WORLDSYSTEM
#define WORLD_WORLDSYSTEM

#include <Core/guid.h>
#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Framework/object/GameObject.h>
#include <memory>
#include <random>
#include <vector>

namespace Engine {
    class LevelAsset;
    class GameObjectAsset;
    class Camera;
    namespace RenderSystemState {
        class CameraManager;
        class SceneDataManager;
    }
    

    class WorldSystem {
    public:
        WorldSystem();
        ~WorldSystem();

        void AddInitEvent();
        void AddTickEvent();

        /// @brief GameObject factory function. Create a GameObject and return a shared pointer to it.
        /// @tparam T T must be derived from GameObject
        /// @tparam ...Args The arguments to be passed to the constructor of T
        /// @return The shared pointer to the created GameObject
        template <typename T, typename... Args>
        std::shared_ptr<T> CreateGameObject(Args... args);

        /// @brief Add a GameObject to the loading queue.
        template <typename T>
        void AddGameObjectToWorld(std::shared_ptr<T> go);

        /// @brief Load all GameObjects in the loading queue. This will call Init() on all components of the
        /// GameObjects.
        void LoadGameObjectInQueue();

        /// @brief Remove a GameObject from the world. TODO: Better removal process instead of simple erase.
        void RemoveGameObjectFromWorld(std::shared_ptr<GameObject> go);

        /// @brief Editor use after a GameObject's components have been modified.
        void RefreshGameObjectInWorld(std::shared_ptr<GameObject> go);

        /// @brief Filter light components and update the light data.
        /// TODO: need futher discussion. Did not use the light manager in scene data manager for now.
        void UpdateLightData(RenderSystemState::SceneDataManager &scene_data_manager);

        /// @brief Load a level asset. Add all GameObjects in the level asset to the loading queue.
        void LoadLevelAsset(std::shared_ptr<LevelAsset> levelAsset);

        /// @brief Load a GameObject asset. Add the GameObject in the asset to the loading queue.
        void LoadGameObjectAsset(std::shared_ptr<GameObjectAsset> gameObjectAsset);

        const std::vector<std::shared_ptr<GameObject>> &GetGameObjects() const;

        std::shared_ptr <Camera> GetActiveCamera() const noexcept;

        /**
         * @brief Set the active camera, and try registering it to the render system.
         * 
         * If registrar if left to be nullptr, no attempts will be made to register it.
         * 
         * @todo Temporary fix. Needs futher discussion.
         */
        void SetActiveCamera(std::shared_ptr<Camera> camera, RenderSystemState::CameraManager * registrar = nullptr) noexcept;

    protected:
        std::vector<std::shared_ptr<GameObject>> m_go_loading_queue{};
        std::vector<std::shared_ptr<GameObject>> m_game_objects{};
        std::vector<std::shared_ptr<Component>> m_all_components{};

        std::shared_ptr<Camera> m_active_camera{};

        uint32_t m_go_id_counter{0};
        uint32_t m_component_id_counter{0};

        void AddComponent(std::shared_ptr<Component> comp);
        void RemoveComponent(std::shared_ptr<Component> comp);
    };

    template <typename T, typename... Args>
    std::shared_ptr<T> WorldSystem::CreateGameObject(Args... args) {
        static_assert(std::is_base_of<GameObject, T>::value, "T must be derived from GameObject");
        GameObject *go_ptr = new T(std::forward<Args>(args)...);
        auto game_object = std::shared_ptr<T>(go_ptr);
        game_object->template AddComponent<TransformComponent>();
        game_object->m_transformComponent = dynamic_pointer_cast<TransformComponent>(game_object->m_components[0]);
        return game_object;
    }

    template <typename T>
    void WorldSystem::AddGameObjectToWorld(std::shared_ptr<T> go) {
        static_assert(std::is_base_of<GameObject, T>::value, "T must be derived from GameObject");
        m_go_loading_queue.push_back(go);
    }
} // namespace Engine
#endif // WORLD_WORLDSYSTEM
