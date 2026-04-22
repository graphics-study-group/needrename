#include "RendererComponent.h"

#include "Framework/object/GameObject.h"
#include "MainClass.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/RendererManager.h"

namespace Engine {
    RendererComponent::RendererComponent(const GameObject &parent) : Component(parent) {
    }

    RendererComponent::~RendererComponent() {
        UnregisterFromRenderSystem();
    }

    Transform RendererComponent::GetWorldTransform() const {
        auto parentGameObject = this->GetParentGameObject();
        assert(parentGameObject && "A renderer component has no parent game object.");
        return parentGameObject->GetWorldTransform();
    }

    void RendererComponent::UnregisterFromRenderSystem() {
        auto mc = MainClass::GetInstance();
        if (!mc) return;
        auto rs = mc->GetRenderSystem();
        if (!rs) return;
        for (auto h : m_renderer_handles) {
            rs->GetRendererManager().Unregister(h);
        }
    }

    void RendererComponent::Awake() {
    }

    void RendererComponent::Tick() {
    }

    void RendererComponent::PreRenderUpdate() {
        if (m_renderer_handles.empty()) return;
        glm::mat4 model = GetWorldTransform().GetTransformMatrix();
        auto *rm = &MainClass::GetInstance()->GetRenderSystem()->GetRendererManager();
        for (auto h : m_renderer_handles) {
            rm->UpdateModelMatrix(h, model);
        }
    }
} // namespace Engine
