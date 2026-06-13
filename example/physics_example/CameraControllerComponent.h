#ifndef EXAMPLE_PHYSICS_EXAMPLE_CAMERACONTROLLERCOMPONENT_H
#define EXAMPLE_PHYSICS_EXAMPLE_CAMERACONTROLLERCOMPONENT_H

#include <Framework/component/Component.h>

namespace Engine {
    class GameObject;
} // namespace Engine

/**
 * @brief Fly camera controller component.
 *
 * Tick() reads WASD/right-mouse-drag input axes and updates the parent
 * GameObject's transform. Right mouse button enables relative mouse mode
 * so the cursor doesn't leave the window while rotating.
 */
class CameraControllerComponent : public Engine::Component {
public:
    explicit CameraControllerComponent(const Engine::GameObject &parent);

    float m_move_speed = 5.0f;
    float m_rotation_speed = 3.0f;

    void Tick() override;

private:
    bool m_relative_mouse_enabled = false;
};

#endif // EXAMPLE_PHYSICS_EXAMPLE_CAMERACONTROLLERCOMPONENT_H
