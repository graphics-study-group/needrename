#include "CameraComponent.h"

#include <Framework/object/GameObject.h>
#include <Render/RenderSystem.h>
#include <MainClass.h>

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

namespace Engine{
    CameraComponent::CameraComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject)
    {
        UpdateViewMatrix();
        UpdateProjectionMatrix();
    }

    void CameraComponent::Init()
    {
        UpdateViewMatrix();
        UpdateProjectionMatrix();
        
        // XXX: This is a hack. We should not set active camera here.
        MainClass::GetInstance()->GetRenderSystem()->SetActiveCamera(std::dynamic_pointer_cast<CameraComponent>(shared_from_this()));
    }

    void CameraComponent::Tick()
    {
        UpdateViewMatrix();
    }

    glm::mat4 CameraComponent::GetViewMatrix() const
    {
        return m_view_matrix;
    }

    glm::mat4 CameraComponent::GetProjectionMatrix() const
    {
        return m_projection_matrix;
    }
    CameraComponent & CameraComponent::set_fov_vertical(float fov)
    {
        m_fov_vertical = fov;
        UpdateProjectionMatrix();
        return *this;
    }
    CameraComponent & CameraComponent::set_aspect_ratio(float aspect)
    {
        m_aspect_ratio = aspect;
        UpdateProjectionMatrix();
        return *this;
    }
    CameraComponent & CameraComponent::set_clipping(float near, float far)
    {
        m_clipping_near = near;
        m_clipping_far = far;
        UpdateProjectionMatrix();
        return *this;
    }
    void CameraComponent::UpdateProjectionMatrix()
    {
        assert(abs(m_fov_vertical) > 1e-3);
        assert(abs(m_aspect_ratio) > 1e-3);
        // assert(abs(m_clipping_near) > 1e-6);
        // assert(abs(m_clipping_far) > 1e-6);
        m_projection_matrix = glm::perspectiveRH(glm::radians(m_fov_vertical), m_aspect_ratio, m_clipping_near, m_clipping_far);
        m_projection_matrix[1][1] *= -1.0f;
    }
    void CameraComponent::UpdateViewMatrix()
    {
        auto parent = m_parentGameObject.lock();
        if (!parent) {
            SDL_LogWarn(0, "Missing parent game object for camera");
            m_view_matrix = glm::mat4(1.0f);
            return;
        }
        Transform transform = parent->GetWorldTransform();
        glm::mat4 tmat = transform.GetTransformMatrix();

        glm::vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};
        origin = tmat * origin;
        glm::vec4 center{0.0f, 1.0f, 0.0f, 1.0f};
        center = tmat * center;
        glm::vec4 up{0.0f, 0.0f, 1.0f, 1.0f};
        up = transform.GetRotation() * up;
        m_view_matrix = glm::lookAtRH(glm::vec3{origin}, glm::vec3{center}, glm::vec3{up});
    }
};
