#ifndef RENDERSYSTEM_SCENEDATAMANAGER
#define RENDERSYSTEM_SCENEDATAMANAGER

#include <memory>
#include <fwd.hpp>

namespace vk {
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
            static constexpr uint32_t MAX_LIGHTS = 8;

        private:
            struct impl;
            std::unique_ptr <impl> pimpl;

        public:
            
            SceneDataManager() noexcept;
            ~SceneDataManager() noexcept;
            void Create(std::shared_ptr<RenderSystem> system);

            /**
             * @brief Set the light tracked by the index to be a directional light.
             */
            void SetLightDirectional(uint32_t index, glm::vec3 direction, glm::vec3 intensity) noexcept;

            // void SetLightPoint(uint32_t index, glm::vec3 direction, glm::vec3 intensity, float radius) noexcept;

            // void SetLightCone(uint32_t index, glm::vec3 direction, glm::vec3 intensity, float inner_angle, float outer_angle) noexcept;

            /**
             * @brief Set a light to be bound to a light component.
             * 
             * Data are fetched from the component at the beginning of drawing process.
             * So if you want to set up light source manually via `SetLightX`
             * methods, you should set the light bound to that index to nullptr.
             */
            void SetLight(uint32_t index, std::shared_ptr <void> light) noexcept;

            /**
             * @brief Set how many lights are there in the scene.
             * 
             * This only affects the drawing process (i.e. how many lights are processed
             * by the shaders), and will not affect any data on the host side.
             */
            void SetLightCount(uint32_t count) noexcept;

            void UploadSceneData(uint32_t frame_in_flight) const noexcept;

            void FetchLightData() noexcept;

            vk::DescriptorSet GetDescriptorSet(uint32_t frame_in_flight) const noexcept;
            vk::DescriptorSetLayout GetDescriptorSetLayout() const noexcept;
        };
    }
}

#endif // RENDERSYSTEM_SCENEDATAMANAGER
