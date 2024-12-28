#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED

#include <glm.hpp>
#include <Framework/component/Component.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    /// @brief A perspective camera component
    class REFL_SER_CLASS(REFL_WHITELIST) CameraComponent : public Component {
        REFL_SER_BODY()
    public:
        REFL_ENABLE CameraComponent() = default;
        REFL_ENABLE CameraComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~CameraComponent() = default;
        virtual void Tick(float) override;

        /// @brief Acquire view matrix that transforms world coordinate to eye coordinate
        /// @return view matrix
        glm::mat4 GetViewMatrix() const;

        /// @brief Acquire projection matrix that transforms eye coordinate to clip space coordinate.
        /// Handness is not flipped by projection matrix.
        /// It should be flipped in vertex shader by postmultipling vec4(1.0, 1.0, -1.0, 1.0).
        /// @return projection matrix
        glm::mat4 GetProjectionMatrix() const;

        /// @brief Set up vertical field of view angle of camera
        /// @param fov angle in degrees
        /// @return this for chainning
        CameraComponent & set_fov_vertical(float fov);

        /// @brief Set up aspect ratio
        /// @param aspect width / height
        /// @return this for chainning
        CameraComponent & set_aspect_ratio(float aspect);

        /// @brief Set up clipping plane coordinate
        /// @param near near clipping coordinate
        /// @param far far clipping coordinate
        /// @return this for chainning
        CameraComponent & set_clipping(float near, float far);

    protected:
        float m_fov_vertical{45};
        float m_aspect_ratio{1.0};
        float m_clipping_near{1e-3};
        float m_clipping_far{1e3};

        glm::mat4 m_projection_matrix{1.0f}, m_view_matrix{1.0f};

        void UpdateProjectionMatrix();
        void UpdateViewMatrix();
    };
} // namespace Engine


#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED
