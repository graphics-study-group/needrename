
#include "CustomComponent.h"

#include "Core/Functional/Time.h"
#include "Framework/object/GameObject.h"
#include "MainClass.h"
#include "UserInterface/Input.h"

#include <SDL3/SDL.h>

using namespace Engine;

SpinningComponent::SpinningComponent(const GameObject &parent) : Component(parent) {
}

void SpinningComponent::Init() {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "SpinningComponent Init");
}

void SpinningComponent::Tick() {
    float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
    auto go = GetParentGameObject();
    if (go) {
        auto &transform = go->GetTransformRef();
        transform.SetRotation(
            transform.GetRotation() * glm::angleAxis(glm::radians(m_speed * dt), glm::vec3(0.0f, 0.0f, 1.0f))
        );
    }
}

ControlComponent::ControlComponent(const GameObject &parent) : Component(parent) {
}

void ControlComponent::Tick() {
    auto input = MainClass::GetInstance()->GetInputSystem();
    auto move_forward = input->GetAxis("move forward");
    auto move_backward = input->GetAxis("move backward");
    auto move_right = input->GetAxis("move right");
    auto move_up = input->GetAxis("move up");
    auto roll_right = input->GetAxisRaw("roll right");
    auto look_x = input->GetAxisRaw("look x");
    auto look_y = input->GetAxisRaw("look y");
    Transform &transform = GetParentGameObject()->GetTransformRef();
    float dt = MainClass::GetInstance()->GetTimeSystem()->GetDeltaTimeInSeconds();
    transform.SetRotation(
        transform.GetRotation()
        * glm::quat(
            glm::vec3{look_y * m_rotation_speed * dt, roll_right * m_roll_speed * dt, look_x * m_rotation_speed * dt}
        )
    );
    transform.SetPosition(
        transform.GetPosition()
        + transform.GetRotation() * glm::vec3{move_right, move_forward + move_backward, move_up} * m_move_speed * dt
    );
}

#include "__generated__/CustomComponent.h.inc"
