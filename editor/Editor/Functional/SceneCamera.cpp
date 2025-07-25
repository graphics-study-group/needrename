#include "SceneCamera.h"
#include <MainClass.h>
#include <Functional/Time.h>
#include <Render/Renderer/Camera.h>

namespace Editor
{
    SceneCamera::SceneCamera()
    {
        m_camera = std::make_shared<Engine::Camera>();
        m_camera->UpdateProjectionMatrix();
        m_camera->UpdateViewMatrix(m_transform);
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
