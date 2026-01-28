#ifndef WORLD_WORLDSYSTEM
#define WORLD_WORLDSYSTEM

#include "Handle.h"
#include <memory>
#include <random>

namespace Engine {
    class Camera;
    class Scene;
    namespace RenderSystemState {
        class CameraManager;
        class SceneDataManager;
    } // namespace RenderSystemState

    class WorldSystem {
    public:
        WorldSystem();
        ~WorldSystem();

        ObjectHandle NextAvailableObjectHandle();
        ComponentHandle NextAvailableComponentHandle();

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

        Scene &GetMainSceneRef() noexcept;

    protected:
        std::shared_ptr<Camera> m_active_camera{};
        std::shared_ptr<Scene> m_main_scene{};

    protected:
        std::mt19937_64 m_go_handle_gen{std::random_device{}()};
        std::mt19937_64 m_comp_handle_gen{std::random_device{}()};
    };
} // namespace Engine
#endif // WORLD_WORLDSYSTEM
