#ifndef WORLD_WORLDSYSTEM
#define WORLD_WORLDSYSTEM

#include "Handle.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {
    class LevelAsset;
    class GameObjectAsset;
    class Camera;
    class GameObject;
    class Component;
    namespace RenderSystemState {
        class CameraManager;
        class SceneDataManager;
    }
    namespace Reflection {
        class Type;
    }

    class WorldSystem {
    public:
        WorldSystem();
        ~WorldSystem();

        static WorldSystem &GetInstance();

        void AddInitEvent();
        void AddTickEvent();

        GameObject &CreateGameObject();
        Component &CreateComponent(ObjectHandle objectHandle, const Reflection::Type &type);
        template <typename T>
        T &CreateComponent(ObjectHandle objectHandle) {
            static_assert(std::is_base_of<Component, T>::value, "T must be derived from Component");
            return static_cast<T &>(AddComponent(objectHandle, new T(objectHandle)));
        }

        void RemoveGameObject(ObjectHandle handle);
        void RemoveComponent(ComponentHandle handle);

        /// @brief Load or remove all GameObjects or Components in the command queue.
        void FlushCmdQueue();

        GameObject *GetGameObject(ObjectHandle handle);
        Component *GetComponent(ComponentHandle handle);
        template <typename T>
        T *GetComponent(ComponentHandle handle) {
            return dynamic_cast<T *>(GetComponent(handle));
        }
        GameObject &GetGameObjectRef(ObjectHandle handle);
        Component &GetComponentRef(ComponentHandle handle);
        template <typename T>
        T &GetComponentRef(ComponentHandle handle) {
            return dynamic_cast<T &>(GetComponentRef(handle));
        }

        const std::vector<std::unique_ptr<GameObject>> &GetGameObjects() const;
        const std::vector<std::unique_ptr<Component>> &GetComponents() const;

        /// @brief Filter light components and update the light data.
        /// TODO: need futher discussion. Did not use the light manager in scene data manager for now.
        void UpdateLightData(RenderSystemState::SceneDataManager &scene_data_manager);

        std::shared_ptr<Camera> GetActiveCamera() const noexcept;

        /**
         * @brief Set the active camera, and try registering it to the render system.
         *
         * If registrar if left to be nullptr, no attempts will be made to register it.
         *
         * @todo Temporary fix. Needs futher discussion.
         */
        void SetActiveCamera(
            std::shared_ptr<Camera> camera, RenderSystemState::CameraManager *registrar = nullptr
        ) noexcept;

    protected:
        std::vector<std::unique_ptr<GameObject>> m_go_add_queue{};
        std::vector<ObjectHandle> m_go_remove_queue{};
        std::vector<std::unique_ptr<Component>> m_comp_add_queue{};
        std::vector<ComponentHandle> m_comp_remove_queue{};

        std::vector<std::unique_ptr<GameObject>> m_game_objects{};
        std::vector<std::unique_ptr<Component>> m_components{};

        std::unordered_map<ObjectHandle, GameObject *> m_go_map{};
        std::unordered_map<ComponentHandle, Component *> m_comp_map{};

        std::shared_ptr<Camera> m_active_camera{};

        ObjectHandle NextAvailableObjectHandle();
        ComponentHandle NextAvailableComponentHandle();

    private:
        ObjectHandle m_go_id_counter{0};
        ComponentHandle m_component_id_counter{0};

        Component &AddComponent(ObjectHandle objectHandle, Component *ptr);
    };
} // namespace Engine
#endif // WORLD_WORLDSYSTEM
