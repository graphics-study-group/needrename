#include "RendererComponent.h"

#include "Framework/object/GameObject.h"
#include "Framework/world/Scene.h"
#include "MainClass.h"
#include "Physics/PhysicsScene.h"
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

        // Determine if the parent GameObject is physics-driven.
        // If so, the vertex shader will read the model matrix from the
        // scene-level model_matrices buffer (set 0 binding 2) instead of
        // using the push-constant model matrix.
        int32_t model_mat_index = -1;
        auto *parentObj = this->GetParentGameObject();
        if (parentObj) {
            auto *scene = parentObj->GetScene();
            if (scene) {
                auto *physicsScene = scene->GetPhysicsScene();
                if (physicsScene) {
                    auto rigid_idx = physicsScene->FindRigidBodyByObjectHandle(parentObj->GetHandle());
                    if (rigid_idx != PhysicsScene::INVALID_INDEX) {
                        model_mat_index = static_cast<int32_t>(rigid_idx);
                    }
                }
            }
        }

        for (auto h : m_renderer_handles) {
            rm->UpdateModelMatrix(h, model);
            rm->UpdateModelMatrixIndex(h, model_mat_index);
        }
    }
} // namespace Engine

#include "__generated__/RendererComponent.h.inc"
