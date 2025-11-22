#ifndef RENDERSYSTEM_CAMERAMANAGER
#define RENDERSYSTEM_CAMERAMANAGER

#include <memory>
// GLM forward declarations.
#include <fwd.hpp>

namespace vk {
    class DescriptorSet;
    class DescriptorSetLayout;
}

namespace Engine {
    class RenderSystem;
    class Camera;

    namespace RenderSystemState {
        /**
         * @brief A class offering unified access to underlying
         * camera data (mainly view and projection matrices).
         */
        class CameraManager {
        public:
            static constexpr uint32_t MAX_CAMERAS = 16;
        private:
            uint32_t m_active_camera_index{};

            struct impl;
            std::unique_ptr <impl> pimpl;
        public:

            CameraManager() noexcept;
            ~CameraManager() noexcept;

            void Create(std::shared_ptr <RenderSystem> system);

            /**
             * @brief Manually write new camera matrices to currently active camera.
             * Effectively calls `WriteCameraMatrices(GetActiveCameraIndex(), ...)`.
             */
            void WriteCameraMatrices(const glm::mat4 & view_matrix, const glm::mat4 & projection_matrix);

            /**
             * @brief Manually write new camera matrices to a camera.
             */
            void WriteCameraMatrices(uint32_t index, const glm::mat4 & view_matrix, const glm::mat4 & projection_matrix);

            /**
             * @brief Upload camera data for a given frame in flight index.
             */
            void UploadCameraData(uint32_t frame_in_flight) const noexcept;

            /**
             * @brief Register a new camera into the manager, possibly substituting an old one.
             * 
             * Pass an empty pointer to unregister all cameras.
             */
            void RegisterCamera(std::weak_ptr <Camera> camera) noexcept;

            /**
             * @brief Fetch camera data from all registered cameras.
             */
            void FetchCameraData();

            vk::DescriptorSet GetDescriptorSet(uint32_t frame_in_flight) const noexcept;
            vk::DescriptorSetLayout GetDescriptorSetLayout() const noexcept;

            void SetActiveCameraIndex(uint32_t index) noexcept;
            uint32_t GetActiveCameraIndex() const noexcept;
        };
    }
}

#endif // RENDERSYSTEM_CAMERAMANAGER
