#ifndef RENDERSYSTEM_SCENEDATAMANAGER
#define RENDERSYSTEM_SCENEDATAMANAGER

#include <memory>
#include <fwd.hpp>

namespace vk {
    class DescriptorSet;
    class DescriptorSetLayout;
    class Extent2D;
}

namespace Engine {
    class RenderSystem;
    class MaterialInstance;
    class GraphicsCommandBuffer;

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
            void SetLightShadowMap(uint32_t index, const RenderTargetTexture & shadowmap) noexcept;

            /**
             * @brief Remove a shadow map from the manager.
             * 
             * As this manager does not track the ownership of the texture,
             * you should *always* call this method after destroying the shadow map.
             */
            void RemoveLightShadowMap(uint32_t index) noexcept;

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
             * @brief Get the number of shadow-casting lights in the scene.
             */
            uint32_t GetNumShadowCastingLights() const noexcept;

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
             * 
             * This method should be called with in a render pass. You can use
             * `GraphicsCommandBuffer::BeginRendering()` or render graph to do
             * this.
             * 
             * @param cb The command buffer to record commands into.
             * @param frame_in_flight The current frame in flight index.
             * @param pv_mat The projection-view matrix for skybox rendering.
             * @param extent The extent of the viewport.
             * 
             * @todo It should be relocated and integrated with GraphicsCommandBuffer.
             */
            void DrawSkybox(GraphicsCommandBuffer & cb, uint32_t frame_in_flight, glm::mat4 pv_mat, const vk::Extent2D &extent) const;

            vk::DescriptorSet GetLightDescriptorSet(uint32_t frame_in_flight) const noexcept;
            vk::DescriptorSetLayout GetLightDescriptorSetLayout() const noexcept;

            /**
             * @brief Acquire a pipeline layout that has descriptor set 0
             * correctly set up according to scene data.
             * 
             * All graphics pipelines should therefore be compatible to
             * this common pipeline layout.
             */
            vk::PipelineLayout GetCommonPipelineLayout() const noexcept;
        };
    }
}

#endif // RENDERSYSTEM_SCENEDATAMANAGER
