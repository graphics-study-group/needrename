#ifndef RENDERSYSTEM_CAMERAMANAGER
#define RENDERSYSTEM_CAMERAMANAGER

#include <memory>
// GLM forward declarations.
#include <fwd.hpp>

namespace vk {
    class DescriptorSet;
}

namespace Engine {
    class RenderSystem;
    namespace RenderSystemState {
        /**
         * @brief A class offering unified access to underlying
         * camera data (mainly view and projection matrices).
         */
        class CameraManager {
        public:
            static constexpr uint32_t MAX_CAMERAS = 16;
        private:
            RenderSystem & m_system;
            uint32_t m_active_camera_index{};

            struct impl;
            std::unique_ptr <impl> pimpl;
        public:

            CameraManager(RenderSystem & system) noexcept;
            ~CameraManager() noexcept;

            void Create();

            /**
             * @brief Write new camera matrices to currently active camera.
             * Effectively calls `WriteCameraMatrices(GetActiveCameraIndex(), ...)`.
             */
            void WriteCameraMatrices(const glm::mat4 & view_matrix, const glm::mat4 & projection_matrix);

            /**
             * @brief Write new camera matrices to a camera.
             */
            void WriteCameraMatrices(uint32_t index, const glm::mat4 & view_matrix, const glm::mat4 & projection_matrix);

            /**
             * @brief Upload camera data for a given frame in flight index.
             */
            void UploadCameraData(uint32_t frame_in_flight) const noexcept;

            vk::DescriptorSet GetDescriptorSet(uint32_t frame_in_flight) const noexcept;

            void SetActiveCameraIndex(uint32_t index) noexcept {
                m_active_camera_index = index;
            }

            uint32_t GetActiveCameraIndex() const noexcept {
                return m_active_camera_index;
            }
        };
    }
}

#endif // RENDERSYSTEM_CAMERAMANAGER
