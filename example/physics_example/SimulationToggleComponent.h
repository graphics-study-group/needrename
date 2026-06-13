#ifndef EXAMPLE_PHYSICS_EXAMPLE_SIMULATIONTOGGLECOMPONENT_H
#define EXAMPLE_PHYSICS_EXAMPLE_SIMULATIONTOGGLECOMPONENT_H

#include <Framework/component/Component.h>

namespace Engine {
    class GameObject;
} // namespace Engine

/**
 * @brief Component that toggles physics simulation on SPACE key press.
 *
 * Uses edge detection on the "toggle simulation" input axis to flip
 * PhysicsScene::m_simulation_enabled once per key press.
 */
class SimulationToggleComponent : public Engine::Component {
public:
    explicit SimulationToggleComponent(const Engine::GameObject &parent);

    void Tick() override;

private:
    bool m_was_pressed = false;
};

#endif // EXAMPLE_PHYSICS_EXAMPLE_SIMULATIONTOGGLECOMPONENT_H
