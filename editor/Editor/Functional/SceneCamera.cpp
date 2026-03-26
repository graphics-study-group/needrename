#include "SceneCamera.h"
#include <Core/Functional/Time.h>
#include <MainClass.h>
#include <Render/Renderer/Camera.h>

namespace Editor {
    SceneCamera::SceneCamera() {
        m_camera = std::make_shared<Engine::Camera>();
        m_camera->UpdateProjectionMatrix();
        m_camera->UpdateViewMatrix(m_transform);
    }

    void SceneCamera::MoveControl(float delta_forward, float delta_right) {
        float dt = Engine::MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
        m_transform.SetPosition(
            m_transform.GetPosition()
            + m_transform.GetRotation() * glm::vec3{delta_right, delta_forward, 0.0f} * m_move_speed * dt
        );
    }

    void SceneCamera::RotateControl(float delta_x, float delta_y) {
        m_yaw -= delta_x * m_rotate_speed;
        m_pitch -= delta_y * m_rotate_speed;

        // Clamp the pitch to prevent gimbal lock
        if (m_pitch > 89.0f) m_pitch = 89.0f;
        if (m_pitch < -89.0f) m_pitch = -89.0f;

        glm::quat qYaw = glm::angleAxis(glm::radians(m_yaw), glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat qPitch = glm::angleAxis(glm::radians(m_pitch), glm::vec3(1.0f, 0.0f, 0.0f));

        m_transform.SetRotation(qYaw * qPitch);
    }
} // namespace Editor
