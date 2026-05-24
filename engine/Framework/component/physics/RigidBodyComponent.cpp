#include "RigidBodyComponent.h"

#include "CollisionShapeComponent.h"

#include <Core/Math/Transform.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>

#include <SDL3/SDL.h>

#include <algorithm>

namespace Engine {
    RigidBodyComponent::RigidBodyComponent(const GameObject &parent) : Component(parent) {
    }

    RigidBodyComponent::~RigidBodyComponent() {
        auto *scene = GetScene();
        if (scene == nullptr || m_rigid_body_index == PhysicsScene::INVALID_INDEX) {
            return;
        }

        if (auto *physics_scene = scene->GetPhysicsScene()) {
            physics_scene->UnregisterRigidBody(m_rigid_body_index);
        }
    }

    void RigidBodyComponent::Awake() {
        auto *scene = GetScene();
        auto *root = GetParentGameObject();
        if (scene == nullptr || root == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "RigidBodyComponent awake failed: missing scene or root object");
            return;
        }

        auto *physics_scene = scene->GetPhysicsScene();
        if (physics_scene == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "RigidBodyComponent awake failed: physics scene missing");
            return;
        }

        std::vector<CollisionShapeComponent *> shapes;
        CollectShapesRecursively(root, shapes, true);

        if (shapes.empty()) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "RigidBodyComponent on object %u has no CollisionShapeComponent in hierarchy",
                root->GetHandle().GetID()
            );
        }

        if (m_rigid_body_index == PhysicsScene::INVALID_INDEX) {
            const uint32_t existing_index = physics_scene->FindRigidBodyByObjectHandle(root->GetHandle());
            if (existing_index != PhysicsScene::INVALID_INDEX) {
                m_rigid_body_index = existing_index;
                physics_scene->SetRigidBodyProperties(
                    m_rigid_body_index,
                    m_mass,
                    m_static_friction,
                    m_dynamic_friction,
                    m_restitution,
                    m_is_kinematic,
                    m_linear_velocity,
                    m_angular_velocity_axis_angle,
                    m_external_force,
                    m_external_torque
                );
            } else {
                m_rigid_body_index = physics_scene->RegisterRigidBody(
                    root->GetHandle(),
                    m_mass,
                    m_static_friction,
                    m_dynamic_friction,
                    m_restitution,
                    m_is_kinematic,
                    m_linear_velocity,
                    m_angular_velocity_axis_angle,
                    m_external_force,
                    m_external_torque
                );
            }
        }

        for (auto *shape_component : shapes) {
            if (!shape_component->IsRegisteredInPhysicsScene()) continue;
            const uint32_t shape_index = shape_component->GetPhysicsShapeIndex();
            if (shape_index == PhysicsScene::INVALID_INDEX) continue;
            physics_scene->SetCollisionShapeRigidBody(shape_index, m_rigid_body_index);
        }
        physics_scene->EnqueueRigidBodyInitialization(m_rigid_body_index);
    }

    uint32_t RigidBodyComponent::GetPhysicsRigidBodyIndex() const noexcept {
        return m_rigid_body_index;
    }

    void RigidBodyComponent::CollectShapesRecursively(
        GameObject *node, std::vector<CollisionShapeComponent *> &shapes, bool skip_rigidbody_check_on_node
    ) {
        if (node == nullptr) {
            return;
        }

        auto *scene = GetScene();
        if (scene == nullptr) {
            return;
        }

        if (!skip_rigidbody_check_on_node) {
            bool has_other_rigidbody = false;
            for (ComponentHandle comp_handle : node->m_components) {
                auto *component = scene->GetComponent(comp_handle);
                auto *rigid = dynamic_cast<RigidBodyComponent *>(component);
                if (rigid != nullptr && rigid != this) {
                    has_other_rigidbody = true;
                    break;
                }
            }
            if (has_other_rigidbody) {
                return;
            }
        }

        for (ComponentHandle comp_handle : node->m_components) {
            auto *component = scene->GetComponent(comp_handle);
            auto *shape = dynamic_cast<CollisionShapeComponent *>(component);
            if (shape) shapes.push_back(shape);
        }

        for (ObjectHandle child_handle : node->GetChildren()) {
            auto *child = scene->GetGameObject(child_handle);
            CollectShapesRecursively(child, shapes, false);
        }
    }

} // namespace Engine

#include "__generated__/RigidBodyComponent.h.inc"
