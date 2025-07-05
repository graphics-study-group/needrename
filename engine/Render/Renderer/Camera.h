#ifndef RENDER_RENDERER_CAMERA_INCLUDED
#define RENDER_RENDERER_CAMERA_INCLUDED

#include <glm.hpp>
#include <Reflection/macros.h>

namespace Engine
{
    class Transform;

    class REFL_SER_CLASS(REFL_BLACKLIST) Camera
    {
        REFL_SER_BODY(Camera)
    public:
        Camera();
        virtual ~Camera() = default;

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
        Camera & set_fov_vertical(float fov);

        /// @brief Set up aspect ratio
        /// @param aspect width / height
        /// @return this for chainning
        Camera & set_aspect_ratio(float aspect);

        /// @brief Set up clipping plane coordinate
        /// @param near near clipping coordinate
        /// @param far far clipping coordinate
        /// @return this for chainning
        Camera & set_clipping(float near, float far);

        REFL_SER_DISABLE void UpdateProjectionMatrix();
        REFL_SER_DISABLE void UpdateViewMatrix(const Transform &transform);

    public:
        float m_fov_vertical{45};
        float m_aspect_ratio{1.0};
        float m_clipping_near{1e-3};
        float m_clipping_far{1e3};
        /// Internal id for the underlying rendering resource of the camera.
        /// _Should_ be unique for each camera that needs to be rendered in _the same frame_.
        unsigned int m_display_id{0};

    protected:
        glm::mat4 m_projection_matrix{1.0f}, m_view_matrix{1.0f};
    };
}

#endif // RENDER_RENDERER_CAMERA_INCLUDED
