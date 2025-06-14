#include "SceneCamera.h"

namespace Editor
{
    SceneCamera::SceneCamera()
    {
        UpdateProjectionMatrix();
        UpdateViewMatrix();
    }

    void SceneCamera::UpdateProjectionMatrix()
    {
        assert(abs(m_fov) > 1e-3);
        assert(abs(m_aspect_ratio) > 1e-3);
        if (m_aspect_ratio > 1.0f)
        {
            m_projection_matrix = glm::perspectiveRH(glm::radians(m_fov), m_aspect_ratio, m_clipping_near, m_clipping_far);
        }
        else
        {
            m_projection_matrix = glm::perspectiveRH(2.0f * atanf(tanf(glm::radians(m_fov) / 2.0f) / m_aspect_ratio), m_aspect_ratio, m_clipping_near, m_clipping_far);
        }
        m_projection_matrix[1][1] *= -1.0f;
    }

    void SceneCamera::UpdateViewMatrix()
    {
        glm::mat4 tmat = m_transform.GetTransformMatrix();
        glm::vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};
        origin = tmat * origin;
        glm::vec4 center{0.0f, 1.0f, 0.0f, 1.0f};
        center = tmat * center;
        glm::vec4 up{0.0f, 0.0f, 1.0f, 1.0f};
        up = m_transform.GetRotation() * up;
        m_view_matrix = glm::lookAtRH(glm::vec3{origin}, glm::vec3{center}, glm::vec3{up});
    }

    void SceneCamera::MoveControl(float delta_forward, float delta_right)
    {
        glm::vec3 forward = m_transform.GetRotation() * glm::vec3{0.0f, 0.0f, -1.0f};
        glm::vec3 right = m_transform.GetRotation() * glm::vec3{1.0f, 0.0f, 0.0f};
        glm::vec3 position = m_transform.GetPosition();
        position += forward * delta_forward * m_move_speed;
        position += right * delta_right * m_move_speed;
        m_transform.SetPosition(position);
    }

    void SceneCamera::RotateControl(float delta_x, float delta_y)
    {
        glm::vec3 rotation = m_transform.GetRotationEuler();
        rotation.x -= glm::radians(delta_y * m_rotate_speed);
        rotation.y += glm::radians(delta_x * m_rotate_speed);
        m_transform.SetRotationEuler(rotation);
    }
}
