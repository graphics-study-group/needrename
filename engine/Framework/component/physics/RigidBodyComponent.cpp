#include "RigidBodyComponent.h"

#include "CollisionShapeComponent.h"

#include <Core/Math/Transform.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Framework/world/physics/PhysicsAdaptor.h>

#include <SDL3/SDL.h>

namespace Engine {
    RigidBodyComponent::RigidBodyComponent(const GameObject &parent) : Component(parent) {
    }

    RigidBodyComponent::~RigidBodyComponent() {
        auto *scene = GetScene();
        if (scene == nullptr || m_rigid_body_index == PhysicsScene::INVALID_INDEX) {
            return;
        }

        auto &adaptor = scene->GetPhysicsAdaptor();
        adaptor.GetPhysicsScene().UnregisterRigidBody(m_rigid_body_index);
    }

    void RigidBodyComponent::Awake() {
        auto *scene = GetScene();
        auto *root = GetParentGameObject();
        if (scene == nullptr || root == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "RigidBodyComponent awake failed: missing scene or root object");
            return;
        }

        auto &adaptor = scene->GetPhysicsAdaptor();
        m_rigid_body_index = adaptor.AllocateSlot(root->GetHandle());
    }

    void RigidBodyComponent::Init() {
        auto *scene = GetScene();
        auto *go = GetParentGameObject();
        if (scene == nullptr || go == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "RigidBodyComponent init failed: missing scene or object");
            return;
        }

        if (m_rigid_body_index == PhysicsScene::INVALID_INDEX) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "RigidBodyComponent init failed: not registered");
            return;
        }

        auto &adaptor = scene->GetPhysicsAdaptor();

        const Transform world_transform = go->GetWorldTransform();

        RigidBodyDescriptor desc;
        desc.mass = m_mass;
        desc.static_friction = m_static_friction;
        desc.dynamic_friction = m_dynamic_friction;
        desc.restitution = m_restitution;
        desc.is_kinematic = m_is_kinematic;
        desc.world_position = world_transform.GetPosition();
        desc.world_rotation = world_transform.GetRotation();
        desc.linear_velocity = m_linear_velocity;
        desc.angular_velocity = m_angular_velocity_axis_angle;
        desc.external_force = m_external_force;
        desc.external_torque = m_external_torque;

        if (m_use_manual_inertia_com) {
            desc.use_manual_inertia_com = true;
            desc.manual_inertia = glm::mat3(
                glm::vec3(m_manual_inertia_diag.x, m_manual_inertia_offdiag.x, m_manual_inertia_offdiag.y),
                glm::vec3(m_manual_inertia_offdiag.x, m_manual_inertia_diag.y, m_manual_inertia_offdiag.z),
                glm::vec3(m_manual_inertia_offdiag.y, m_manual_inertia_offdiag.z, m_manual_inertia_diag.z)
            );
            desc.manual_center_of_mass = m_manual_center_of_mass;
        }

        adaptor.SubmitRigidBody(m_rigid_body_index, desc);
        CollectShapesRecursivelyAndBind(go, adaptor, true);
    }

    uint32_t RigidBodyComponent::GetPhysicsRigidBodyIndex() const noexcept {
        return m_rigid_body_index;
    }

    void RigidBodyComponent::CollectShapesRecursivelyAndBind(
        GameObject *node, PhysicsAdaptor &adaptor, bool skip_rigidbody_check_on_node
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
            if (shape && shape->IsRegisteredInPhysicsScene()) {
                const uint32_t shape_index = shape->GetPhysicsShapeIndex();
                if (shape_index != PhysicsScene::INVALID_INDEX) {
                    adaptor.BindShapeToRigidBody(shape_index, m_rigid_body_index);
                }
            }
        }

        for (ObjectHandle child_handle : node->GetChildren()) {
            auto *child = scene->GetGameObject(child_handle);
            CollectShapesRecursivelyAndBind(child, adaptor, false);
        }
    }

} // namespace Engine

#include "__generated__/RigidBodyComponent.h.inc"
