#include "CameraComponent.h"

#include "Framework/go/GameObject.h"

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

namespace Engine{
    CameraComponent::CameraComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject)
    {
    }

    void CameraComponent::Tick(float)
    {
    }

    glm::mat4 CameraComponent::GetViewMatrix() const
    {
        auto parent = m_parentGameObject.lock();
        if (!parent) {
            SDL_LogWarn(0, "Missing parent game object for camera");
            return glm::mat4(1.0f);
        }
        Transform transform = parent->GetWorldTransform();
        glm::mat4 tmat = transform.GetTransformMatrix();

        glm::vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};
        origin = tmat * origin;

        // Y axis points front, so we just transform (0,1,0) to get local front vector
        glm::vec4 front{0.0f, 1.0f, 0.0f, 1.0f};
        front = tmat * front;

        return glm::lookAtRH(glm::vec3{origin}, glm::vec3{front}, glm::vec3{0.0, 0.0, 1.0});
    }

    glm::mat4 CameraComponent::GetProjectionMatrix() const
    {
        assert(abs(m_fov_vertical) > 1e-3);
        assert(abs(m_aspect_ratio) > 1e-3);
        // assert(abs(m_clipping_near) > 1e-6);
        // assert(abs(m_clipping_far) > 1e-6);
        return glm::perspectiveRH(glm::radians(m_fov_vertical), m_aspect_ratio, m_clipping_near, m_clipping_far);
    }
    CameraComponent & CameraComponent::set_fov_vertical(float fov)
    {
        m_fov_vertical = fov;
        return *this;
    }
    CameraComponent & CameraComponent::set_aspect_ratio(float aspect)
    {
        m_aspect_ratio = aspect;
        return *this;
    }
    CameraComponent & CameraComponent::set_clipping(float near, float far)
    {
        m_clipping_near = near;
        m_clipping_far = far;
        return *this;
    }
};
