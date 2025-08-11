#ifndef EDITOR_FUNCTIONAL_SCENECAMERA_INCLUDED
#define EDITOR_FUNCTIONAL_SCENECAMERA_INCLUDED

#include <Core/Math/Transform.h>
#include <glm.hpp>

namespace Engine {
    class Camera;
}

namespace Editor {
    using Transform = Engine::Transform;

    class SceneCamera {
    public:
        SceneCamera();
        ~SceneCamera() = default;

        void MoveControl(float delta_forward, float delta_right);
        void RotateControl(float delta_x, float delta_y);

    public:
        float m_move_speed{1.0f};
        float m_rotate_speed{0.2f};

        Transform m_transform{};

        std::shared_ptr<Engine::Camera> m_camera{};
    };
} // namespace Editor

#endif // EDITOR_FUNCTIONAL_SCENECAMERA_INCLUDED
