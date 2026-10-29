#ifndef FRAMEWORK_WORLD_WORLDSYSTEM_INCLUDED
#define FRAMEWORK_WORLD_WORLDSYSTEM_INCLUDED

#include "Framework/framework_export.h"
#include "Handle.h"
#include <Asset/AssetRef.h>
#include <memory>
#include <random>
#include <unordered_map>
#include <unordered_set>

namespace Engine {
    class Camera;
    class Scene;
    class LevelAsset;
    class LightComponent;
    class RenderSystem;
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
    class FRAMEWORK_API WorldSystem {
    public:
        WorldSystem();
        ~WorldSystem();

        /// @brief Update all renderer-related data before rendering.
        /// This includes model matrices (RendererComponent) and light data (LightComponent).
        void UpdateRendererData(RenderSystem &render_system);

        /**
         * @brief Get the active camera.
         * TODO: 'Camera' is not good. Needs futher discussion.
         */
        std::shared_ptr<Camera> GetActiveCamera() const noexcept;

        /**
         * @brief Get the directional light that holds the scene's shadow-caster role.
         *
         * Only one directional light casts shadows at a time. The role belongs
         * to the first directional light whose `m_cast_shadow` flag is set in the
         * scene's component order, which is creation order, so it never depends on
         * colour, intensity, handles or hierarchy order. `UpdateLightData` applies
         * the same rule while it walks the components.
         *
         * @return The role holder, or nullptr when no directional light in the
         *         main scene has the flag set.
         */
        LightComponent *GetShadowCastingLight() const noexcept;

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

    private:
        /// @brief Filter light components and update the light data.
        /// TODO: need futher discussion. Did not use the light manager in scene data manager for now.
        void UpdateLightData(RenderSystemState::SceneDataManager &scene_data_manager);

        /// Component IDs of the flagged lights already warned about being
        /// demoted to unshadowed lighting. Kept so a hand-edited scene with
        /// several flagged lights warns once instead of once per frame.
        std::unordered_set<uint32_t> m_demoted_shadow_lights{};
    };
} // namespace Engine
#endif // FRAMEWORK_WORLD_WORLDSYSTEM_INCLUDED
