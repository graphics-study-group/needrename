#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED

#include <glm.hpp>
#include "Framework/component/Component.h"

namespace Engine
{
    struct CameraContext{
        glm::mat4 view_matrix;
        glm::mat4 projection_matrix;
    };

    /// @brief A perspective camera component
    class CameraComponent : public Component {
    public:
        CameraComponent(std::weak_ptr<GameObject> gameObject);
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

        /// @brief Create a new camera context
        /// @return context
        CameraContext CreateContext() const;

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
    };
} // namespace Engine


#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_CAMERACOMPONENT_INCLUDED
