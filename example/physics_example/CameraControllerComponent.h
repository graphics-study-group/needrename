#ifndef EXAMPLE_PHYSICS_EXAMPLE_CAMERACONTROLLERCOMPONENT_H
#define EXAMPLE_PHYSICS_EXAMPLE_CAMERACONTROLLERCOMPONENT_H

#include <Framework/component/Component.h>

namespace Engine {
    class GameObject;
} // namespace Engine

/**
 * @brief Fly camera controller component with Z-up locked.
 *
 * Yaw rotates around world Z, pitch rotates around world X.
 * Angles are accumulated each frame and the rotation is rebuilt from scratch
 * to prevent drift (follows the same pattern as Editor::SceneCamera).
 *
 * Tick() reads WASD/right-mouse-drag input axes and updates the parent
 * GameObject's transform. Right mouse button enables relative mouse mode.
 */
class CameraControllerComponent : public Engine::Component {
public:
    explicit CameraControllerComponent(const Engine::GameObject &parent);

    float m_move_speed = 5.0f;
    float m_rotation_speed = 0.3f;

    /// Accumulated yaw angle in degrees (rotation around world Z).
    float m_yaw = 0.0f;

    /// Accumulated pitch angle in degrees (rotation around world X).
    float m_pitch = 0.0f;

    void Tick() override;

private:
    bool m_relative_mouse_enabled = false;
};

#endif // EXAMPLE_PHYSICS_EXAMPLE_CAMERACONTROLLERCOMPONENT_H
