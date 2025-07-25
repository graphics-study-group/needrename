#include "Camera.h"
#include <Core/Math/Transform.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

namespace Engine {
    Camera::Camera() {
        UpdateProjectionMatrix();
    }

    glm::mat4 Camera::GetViewMatrix() {
        return m_view_matrix;
    }

    glm::mat4 Camera::GetProjectionMatrix() {
        if (m_is_proj_dirty) {
            UpdateProjectionMatrix();
        }
        return m_projection_matrix;
    }

    Camera &Camera::set_fov_vertical(float fov) {
        m_is_proj_dirty = true;
        m_fov_vertical = fov;
        return *this;
    }

    Camera &Camera::set_fov_horizontal(float fov) {
        m_is_proj_dirty = true;
        // Convert horizontal FOV to vertical FOV based on aspect ratio
        m_fov_vertical = glm::degrees(2.0f * atanf(tanf(glm::radians(fov) / 2.0f) * m_aspect_ratio));
        return *this;
    }

    Camera &Camera::set_aspect_ratio(float aspect) {
        m_is_proj_dirty = true;
        m_aspect_ratio = aspect;
        return *this;
    }

    Camera &Camera::set_clipping(float near, float far) {
        m_is_proj_dirty = true;
        m_clipping_near = near;
        m_clipping_far = far;
        return *this;
    }

    void Camera::UpdateProjectionMatrix() {
        assert(abs(m_fov_vertical) > 1e-3);
        assert(abs(m_aspect_ratio) > 1e-3);
        m_projection_matrix =
            glm::perspectiveRH(glm::radians(m_fov_vertical), m_aspect_ratio, m_clipping_near, m_clipping_far);
        m_projection_matrix[1][1] *= -1.0f;
        m_is_proj_dirty = false;
    }

    void Camera::UpdateViewMatrix(const Transform &transform) {
        glm::mat4 tmat = transform.GetTransformMatrix();
        glm::vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};
        origin = tmat * origin;
        glm::vec4 center{0.0f, 1.0f, 0.0f, 1.0f};
        center = tmat * center;
        glm::vec4 up{0.0f, 0.0f, 1.0f, 1.0f};
        up = transform.GetRotation() * up;
        m_view_matrix = glm::lookAtRH(glm::vec3{origin}, glm::vec3{center}, glm::vec3{up});
    }
} // namespace Engine
