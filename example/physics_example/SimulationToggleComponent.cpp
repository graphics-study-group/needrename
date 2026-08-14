#include "SimulationToggleComponent.h"

#include "Framework/Component/Component.h"
#include "Framework/Input/Input.h"
#include "Framework/MainClass.h"
#include "Framework/World/Scene.h"
#include "Physics/PhysicsScene.h"

#include <SDL3/SDL.h>

using namespace Engine;

SimulationToggleComponent::SimulationToggleComponent(const Engine::GameObject &parent) : Component(parent) {
}

void SimulationToggleComponent::Tick() {
    auto input = MainClass::GetInstance()->GetInputSystem();
    float current = input->GetAxisRaw("toggle simulation");
    if (current > 0.0f && !m_was_pressed) {
        auto *physics_scene = GetScene()->GetPhysicsScene();
        if (physics_scene) {
            physics_scene->SetSimulationEnabled(!physics_scene->IsSimulationEnabled());
            SDL_LogInfo(
                SDL_LOG_CATEGORY_APPLICATION,
                "Simulation %s",
                physics_scene->IsSimulationEnabled() ? "resumed" : "paused"
            );
        }
    }
    m_was_pressed = (current > 0.0f);
}
