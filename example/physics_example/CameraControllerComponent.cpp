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

    // Right mouse button: manage relative mouse mode for comfortable look.
    auto window = MainClass::GetInstance()->GetWindow()->GetWindow();
    if (mouse_right > 0.0f) {
        if (!m_relative_mouse_enabled) {
            SDL_SetWindowRelativeMouseMode(window, true);
            m_relative_mouse_enabled = true;
        }
        transform.SetRotation(
            transform.GetRotation()
            * glm::quat(glm::vec3{look_y * m_rotation_speed * dt, 0.0f, look_x * m_rotation_speed * dt})
        );
    } else {
        if (m_relative_mouse_enabled) {
            SDL_SetWindowRelativeMouseMode(window, false);
            m_relative_mouse_enabled = false;
        }
    }

    // WASD + QE movement in camera-local space (RFU: X=right, Y=forward, Z=up).
    glm::vec3 movement{move_right, move_forward, move_up};
    transform.SetPosition(transform.GetPosition() + transform.GetRotation() * movement * m_move_speed * dt);
}
