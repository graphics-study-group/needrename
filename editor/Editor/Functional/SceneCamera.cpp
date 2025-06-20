#include "SceneCamera.h"
#include <MainClass.h>
#include <Functional/Time.h>

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
        float dt = Engine::MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
        m_transform.SetPosition(m_transform.GetPosition() + m_transform.GetRotation() * glm::vec3{delta_right, delta_forward, 0.0f} * m_move_speed * dt);
    }

    void SceneCamera::RotateControl(float delta_x, float delta_y)
    {
        m_transform.SetRotation(m_transform.GetRotation() * glm::quat(glm::vec3{-glm::radians(delta_y * m_rotate_speed), 0.0f, -glm::radians(delta_x * m_rotate_speed)}));
    }
}
