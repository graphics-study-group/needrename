#ifndef APPPHYSICS_CAMERACONTROLLERCOMPONENT_H
#define APPPHYSICS_CAMERACONTROLLERCOMPONENT_H

#include <Framework/Component/Component.h>

namespace Engine {
    class GameObject;
} // namespace Engine

namespace AppPhysics {

    /**
     * @brief Fly camera controller component with Z-up locked.
     *
     * Yaw rotates around world Z, pitch rotates around world X.
     * Angles are accumulated each frame and the rotation is rebuilt from scratch
     * to prevent drift. Tick() reads WASD/QE input axes plus the right-mouse look
     * axes and updates the parent GameObject's transform only while an Alt
     * modifier is held. The Alt gate covers both flight and mouse look (relative
     * mouse mode is engaged only for Alt + right mouse), so no stray key press or
     * drag can move the camera.
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
} // namespace AppPhysics

#endif // APPPHYSICS_CAMERACONTROLLERCOMPONENT_H
