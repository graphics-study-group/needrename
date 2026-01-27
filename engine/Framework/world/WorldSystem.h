#ifndef WORLD_WORLDSYSTEM
#define WORLD_WORLDSYSTEM

#include "Handle.h"
#include <Framework/component/TransformComponent/TransformComponent.h>
#include <Framework/object/GameObject.h>
#include <memory>
#include <queue>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Engine {
    class LevelAsset;
    class GameObjectAsset;
    class Camera;
    namespace RenderSystemState {
        class CameraManager;
        class SceneDataManager;
    } // namespace RenderSystemState

    class WorldSystem {
    public:
        WorldSystem();
        ~WorldSystem();

        void AddInitEvent();
        void AddTickEvent();

        ObjectHandle CreateGameObject();
        ComponentHandle CreateComponent(ObjectHandle objectHandle);

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
        struct ObjectCmd {
            enum {
                Add,
                Remove
            } cmd;
            std::variant<std::unique_ptr<GameObject>, ObjectHandle> go;
        };
        std::queue<ObjectCmd> m_go_cmd_queue{};

        struct ComponentCmd {
            enum {
                Add,
                Remove
            } cmd;
            std::variant<std::unique_ptr<Component>, ComponentHandle> comp;
        };
        std::queue<ComponentCmd> m_comp_cmd_queue{};

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
    };
} // namespace Engine
#endif // WORLD_WORLDSYSTEM
