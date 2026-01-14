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
             * @brief Set the current skybox cubemap to be the new texture.
             * 
             * The manager assumes that the cubemap assigned is sychronized correctly,
             * and takes the layout `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL` before
             * sampling from it. You can use `UseImage(*shadow, ShaderRead)`
             * or similar to synchronize its access.
             */
            void SetSkyboxCubemap(std::shared_ptr <Texture> texture) noexcept;

            void UploadSceneData(uint32_t frame_in_flight) const noexcept;

            void FetchLightData() noexcept;

            /**
             * @brief Record commands for drawing a skybox.
             * 
             * @todo It should be relocated and integrated with GraphicsCommandBuffer.
             */
            void DrawSkybox(vk::CommandBuffer cb, MaterialLibrary & library, uint32_t frame_in_flight, glm::mat4 pv) const;

            vk::DescriptorSet GetLightDescriptorSet(uint32_t frame_in_flight) const noexcept;
            vk::DescriptorSetLayout GetLightDescriptorSetLayout() const noexcept;

            vk::DescriptorSet GetSkyboxDescriptorSet(uint32_t frame_in_flight) const noexcept;
            vk::DescriptorSetLayout GetSkyboxDescriptorSetLayout() const noexcept;
        };
    }
}

#endif // RENDERSYSTEM_SCENEDATAMANAGER
