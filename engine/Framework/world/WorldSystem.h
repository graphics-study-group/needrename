#ifndef WORLD_WORLDSYSTEM
#define WORLD_WORLDSYSTEM

#include "Handle.h"
#include <Asset/AssetRef.h>
#include <memory>
#include <random>
#include <unordered_map>

namespace Engine {
    class Camera;
    class Scene;
    class LevelAsset;
    namespace RenderSystemState {
        class CameraManager;
        class SceneDataManager;
    } // namespace RenderSystemState

    /**
     * @brief WorldSystem manages multiple Scene instances.
     * Each Scene can be accessed via a uint32_t scene ID.
     * A main Scene exists inside World, representing the active runtime scene.
     * WorldSystem is responsible for organizing scene data (cameras, light sources, etc.)
     * and renderer data in the game, and transmitting them to the rendering system every frame.
     */
    class WorldSystem {
    public:
        WorldSystem();
        ~WorldSystem();

        /// @brief Filter light components and update the light data.
        /// TODO: need futher discussion. Did not use the light manager in scene data manager for now.
        void UpdateLightData(RenderSystemState::SceneDataManager &scene_data_manager);

        /**
         * @brief Get the active camera.
         * TODO: 'Camera' is not good. Needs futher discussion.
         */
        std::shared_ptr<Camera> GetActiveCamera() const noexcept;

        /**
         * @brief Set the active camera, and try registering it to the render system.
         *
         * If registrar if left to be nullptr, no attempts will be made to register it.
         *
         * @todo Temporary fix. Needs futher discussion.
         */
        void SetActiveCamera(
            ComponentHandle camera_comp, RenderSystemState::CameraManager *registrar = nullptr
        ) noexcept;

        /**
         * @brief Get the main scene.
         * The main scene is the active runtime scene in the game.
         */
        Scene &GetMainSceneRef();

        /**
         * @brief Get a scene by its ID.
         * @param sceneID The ID of the scene to get.
         * @return The reference to the scene.
         */
        Scene &GetSceneRef(uint32_t sceneID);

        /**
         * @brief Get a scene by its ID.
         * @param sceneID The ID of the scene to get.
         * @return The pointer to the scene.
         */
        Scene *GetScenePtr(uint32_t sceneID);

        /**
         * @brief Create a new scene.
         * @return The reference to the created scene.
         */
        Scene &CreateScene();

        /**
         * @brief Clear all unused scenes.
         * This method is called every frame to ensure that only active scenes are kept in memory.
         *
         * TODO: Currently implement shared_ptr to manage scene memory. May need better management.
         */
        void ClearUnusedScenes();

        /**
         * @brief Save the main scene to a level_asset.
         * @param level_asset The level_asset to save the scene to.
         */
        void SaveLevelToAsset(LevelAsset &level_asset);

    protected:
        ComponentHandle m_active_camera{};
        std::shared_ptr<Scene> m_main_scene{};

        std::unordered_map<uint32_t, std::shared_ptr<Scene>> m_scene_map{};
        uint32_t m_scene_id_gen{1};

    public:
        AssetRef m_skybox_material{};
    };
} // namespace Engine
#endif // WORLD_WORLDSYSTEM
