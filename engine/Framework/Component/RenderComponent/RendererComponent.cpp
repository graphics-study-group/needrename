#include "RendererComponent.h"

#include "Framework/Bridge/PhysicsAdaptor.h"
#include "Framework/MainClass.h"
#include "Framework/Object/GameObject.h"
#include "Framework/World/Scene.h"
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

        // Walk up the ancestor chain to find a registered RigidBody.
        // Matches the "connected block" semantics: stops at the nearest ancestor
        // with a RigidBody (analogous to CollisionShapeComponent::TryAttachToAncestorRigidBody).
        int32_t model_mat_index = -1;
        auto *currentObj = this->GetParentGameObject();
        if (currentObj) {
            auto *scene = currentObj->GetScene();
            if (scene) {
                auto &adaptor = scene->GetPhysicsAdaptor();
                if (adaptor.IsPhysicsActive()) {
                    while (currentObj) {
                        auto rigid_idx = adaptor.FindRigidBodyByObjectHandle(currentObj->GetHandle());
                        if (rigid_idx != PhysicsScene::INVALID_INDEX) {
                            model_mat_index = static_cast<int32_t>(rigid_idx);
                            // Compute local transform relative to the rigid body's COM.
                            // The shader will compose: model_matrices[index] * pc.model.
                            glm::vec3 com_offset = adaptor.GetComOffsetLocal(rigid_idx);
                            Transform rb_tr = currentObj->GetWorldTransform();
                            rb_tr.SetScale(glm::vec3(1.0f)); // The solver does not apply scaling, so we ignore it
                            glm::mat4 rb_world = rb_tr.GetTransformMatrix();
                            model = glm::translate(glm::mat4(1.0f), -com_offset) * glm::inverse(rb_world) * model;
                            break;
                        }
                        // Move up to parent.
                        auto parent_handle = currentObj->GetParent();
                        if (!parent_handle.IsValid()) break;
                        currentObj = scene->GetGameObject(parent_handle);
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
