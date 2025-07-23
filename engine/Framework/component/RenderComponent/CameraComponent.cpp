#include "CameraComponent.h"

#include <Framework/object/GameObject.h>
#include <Render/RenderSystem.h>
#include <Render/Renderer/Camera.h>
#include <MainClass.h>

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

namespace Engine{
    CameraComponent::CameraComponent(std::weak_ptr<GameObject> gameObject) : Component(gameObject)
    {
        m_camera = std::make_shared<Camera>();
        UpdateViewMatrix();
        UpdateProjectionMatrix();
    }

    void CameraComponent::Init()
    {
    }

    void CameraComponent::Tick()
    {
        UpdateViewMatrix();
    }

    void CameraComponent::UpdateProjectionMatrix()
    {
        m_camera->UpdateProjectionMatrix();
    }
    void CameraComponent::UpdateViewMatrix()
    {
        auto parent = m_parentGameObject.lock();
        if (!parent) {
            SDL_LogWarn(0, "Missing parent game object for camera");
            return;
        }
        m_camera->UpdateViewMatrix(parent->GetWorldTransform());
    }
};
