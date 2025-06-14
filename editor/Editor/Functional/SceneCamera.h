#ifndef EDITOR_FUNCTIONAL_SCENECAMERA_INCLUDED
#define EDITOR_FUNCTIONAL_SCENECAMERA_INCLUDED

#include <glm.hpp>
#include <Core/Math/Transform.h>

namespace Editor
{
    using Transform = Engine::Transform;

    class SceneCamera
    {
    public:
        SceneCamera();
        ~SceneCamera() = default;

        void UpdateProjectionMatrix();
        void UpdateViewMatrix();
    
    public:
        float m_move_speed{1.0f};
        float m_rotate_speed{0.2f};

        Transform m_transform{};

        float m_fov{45};
        float m_aspect_ratio{1.0};
        float m_clipping_near{1e-3};
        float m_clipping_far{1e3};
    
        glm::mat4 m_projection_matrix{1.0f}, m_view_matrix{1.0f};
    };
}

#endif // EDITOR_FUNCTIONAL_SCENECAMERA_INCLUDED
