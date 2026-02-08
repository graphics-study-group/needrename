#ifndef WORLD_WORLDSYSTEM
#define WORLD_WORLDSYSTEM

#include "Handle.h"
#include <memory>
#include <random>
#include <unordered_map>

namespace Engine {
    class Camera;
    class Scene;
    class AssetRef;
    namespace RenderSystemState {
        class CameraManager;
        class SceneDataManager;
    } // namespace RenderSystemState

    namespace Serialization {
        class Archive;
    }

    class WorldSystem {
    public:
        WorldSystem();
        ~WorldSystem();

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

        Scene &GetMainSceneRef();
        Scene &GetSceneRef(uint32_t sceneID);
        Scene *GetScenePtr(uint32_t sceneID);
        Scene &CreateScene();
        void RemoveScene(uint32_t sceneID);

        /**
         * @brief Save the main scene to the archive.
         * Temporary use.
         */
        void SaveLevelToArchive(Serialization::Archive &archive);

    protected:
        std::shared_ptr<Camera> m_active_camera{};
        std::shared_ptr<Scene> m_main_scene{};

        std::unordered_map<uint32_t, std::shared_ptr<Scene>> m_scene_map{};
        uint32_t m_scene_id_gen{1};
    public:
        std::shared_ptr<AssetRef> m_skybox_material{};
    };
} // namespace Engine
#endif // WORLD_WORLDSYSTEM
