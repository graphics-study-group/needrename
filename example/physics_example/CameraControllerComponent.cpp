#include "CameraControllerComponent.h"

#include "Core/Functional/SDLWindow.h"
#include "Core/Functional/Time.h"
#include "Core/Math/Transform.h"
#include "Framework/object/GameObject.h"
#include "MainClass.h"
#include "UserInterface/Input.h"

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <gtc/quaternion.hpp>

using namespace Engine;

CameraControllerComponent::CameraControllerComponent(const Engine::GameObject &parent) : Component(parent) {
}

void CameraControllerComponent::Tick() {
    auto input = MainClass::GetInstance()->GetInputSystem();
    auto move_forward = input->GetAxis("move forward");
    auto move_right = input->GetAxis("move right");
    auto move_up = input->GetAxis("move up");
    auto look_x = input->GetAxisRaw("look x");
    auto look_y = input->GetAxisRaw("look y");
    auto mouse_right = input->GetAxisRaw("mouse right");

    auto go = GetParentGameObject();
    if (!go) return;
    Transform &transform = go->GetTransformRef();
    float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();

    // Right mouse button: manage relative mouse mode.
    auto window = MainClass::GetInstance()->GetWindow()->GetWindow();
    if (mouse_right > 0.0f) {
        if (!m_relative_mouse_enabled) {
            SDL_SetWindowRelativeMouseMode(window, true);
            m_relative_mouse_enabled = true;
        }
    } else {
        if (m_relative_mouse_enabled) {
            SDL_SetWindowRelativeMouseMode(window, false);
            m_relative_mouse_enabled = false;
        }
    }

    // --- Lock Z-up: same pattern as Editor::SceneCamera::RotateControl ---
    // Accumulate yaw/pitch in degrees (only when right mouse is held),
    // then rebuild rotation from scratch using world axes only.
    // This guarantees Z-up never drifts.
    if (mouse_right > 0.0f) {
        m_yaw += look_x * m_rotation_speed;
        m_pitch += look_y * m_rotation_speed;

        // Clamp pitch to prevent gimbal lock.
        if (m_pitch > 89.0f) m_pitch = 89.0f;
        if (m_pitch < -89.0f) m_pitch = -89.0f;
    }

    // Always rebuild rotation from accumulated angles so the initial
    // yaw/pitch (set in main.cpp) are applied on the first frame.
    {
        glm::quat qYaw = glm::angleAxis(glm::radians(m_yaw), glm::vec3(0.0f, 0.0f, 1.0f));
        glm::quat qPitch = glm::angleAxis(glm::radians(m_pitch), glm::vec3(1.0f, 0.0f, 0.0f));
        transform.SetRotation(qYaw * qPitch);
    }

    // WASD + QE movement in camera-local space (RFU: X=right, Y=forward, Z=up).
    glm::vec3 movement{move_right, move_forward, move_up};
    transform.SetPosition(transform.GetPosition() + transform.GetRotation() * movement * m_move_speed * dt);
}
