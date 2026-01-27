#ifndef RENDERSYSTEM_SCENEDATAMANAGER
#define RENDERSYSTEM_SCENEDATAMANAGER

#include <memory>
#include <fwd.hpp>

namespace vk {
    class CommandBuffer;
    class DescriptorSet;
    class DescriptorSetLayout;
}

namespace Engine {
    class RenderSystem;
    class MaterialInstance;
    namespace RenderSystemState {
        /**
         * @brief Aggregated manager for scene data, such as lights and skybox.
         */
        class SceneDataManager {
        public:
            static constexpr uint32_t MAX_SHADOW_CASTING_LIGHTS = 8;
            static constexpr uint32_t MAX_NON_SHADOW_CASTING_LIGHTS = 16;

        private:
            RenderSystem & m_system;
            struct impl;
            std::unique_ptr <impl> pimpl;

        public:
            
            SceneDataManager(RenderSystem & system) noexcept;
            ~SceneDataManager() noexcept;
            void Create();

            /**
             * @brief Set the shadow-casting light tracked by the index to be a directional light.
             */
            void SetLightDirectional(uint32_t index, glm::vec3 direction, glm::vec3 intensity) noexcept;

            void SetLightPoint(uint32_t index, glm::vec3 direction, glm::vec3 intensity, float radius) noexcept;

            void SetLightCone(uint32_t index, glm::vec3 direction, glm::vec3 intensity, float inner_angle, float outer_angle) noexcept;

            /**
             * @brief Set the none shadow-casting light tracked by the index to be a directional light.
             */
            void SetLightDirectionalNonShadowCasting(uint32_t index, glm::vec3 direction, glm::vec3 intensity) noexcept;

            /**
             * @brief Set the none shadow-casting light tracked by the index to be a point light.
             */
            void SetLightPointNonShadowCasting(uint32_t index, glm::vec3 position, glm::vec3 intensity) noexcept;

            /**
             * @brief Set the shadow map of a shadow-casting light
             * 
             * This manager does not obtain the ownership of the texture.
             * 
             * The manager assumes that the shadowmap assigned is sychronized correctly,
             * and takes the layout `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL` before
             * sampling from it. You should use `UseImage(*shadow, ShaderRead)`
             * or similar to synchronize its access.
             */
            void SetLightShadowMap(uint32_t index, std::weak_ptr <RenderTargetTexture> shadowmap) noexcept;

            /**
             * @brief Set a shadow-casting light to be bound to a light component.
             * 
             * Data are fetched from the component at the beginning of drawing process.
             * So if you want to set up light source manually via `SetLightX`
             * methods, you should set the light bound to that index to nullptr.
             */
            void SetLight(uint32_t index, std::shared_ptr <void> light) noexcept;

            /**
             * @brief Set a non shadow-casting light to be bound to a light component.
             * 
             * Data are fetched from the component at the beginning of drawing process.
             * So if you want to set up light source manually via `SetLightX`
             * methods, you should set the light bound to that index to nullptr.
             */
            void SetLightNonShadowCasting(uint32_t index, std::shared_ptr <void> light) noexcept;

            /**
             * @brief Set how many shadow-casting  lights are there in the scene.
             * 
             * This only affects the drawing process (i.e. how many lights are processed
             * by the shaders), and will not affect any data on the host side.
             */
            void SetLightCount(uint32_t count) noexcept;

            /**
             * @brief Set how many none shadow-casting lights are there in the scene.
             * 
             * This only affects the drawing process (i.e. how many lights are processed
             * by the shaders), and will not affect any data on the host side.
             */
            void SetLightCountNonShadowCasting(uint32_t count) noexcept;

            /**
             * @brief Set the current skybox material.
             * 
             * The manager assumes that the material instance contains a MaterialLibrary which set a MaterialTemplate
             * with tag `SKYBOX`. The shader should not use any scene descriptor set.
             */
            void SetSkyboxMaterial(std::shared_ptr <MaterialInstance> material) noexcept;

            void UploadSceneData(uint32_t frame_in_flight) const noexcept;

            void FetchLightData() noexcept;

            /**
             * @brief Record commands for drawing a skybox.
             * @param cb The command buffer to record commands into.
             * @param frame_in_flight The current frame in flight index.
             * @param view_mat The view matrix of the current camera. 3x3 matrix (no translation).
             * @param proj_mat The projection matrix of the current camera.
             * 
             * @todo It should be relocated and integrated with GraphicsCommandBuffer.
             */
            void DrawSkybox(vk::CommandBuffer cb, uint32_t frame_in_flight, glm::mat3 view_mat, glm::mat4 proj_mat) const;

            vk::DescriptorSet GetLightDescriptorSet(uint32_t frame_in_flight) const noexcept;
            vk::DescriptorSetLayout GetLightDescriptorSetLayout() const noexcept;
        };
    }
}

#endif // RENDERSYSTEM_SCENEDATAMANAGER
