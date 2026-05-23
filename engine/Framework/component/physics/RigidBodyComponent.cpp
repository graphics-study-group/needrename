#include "RigidBodyComponent.h"

#include "CollisionShapeComponent.h"

#include <Core/Math/Transform.h>
#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <deque>

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

        std::vector<ShapeCollectRecord> records;
        CollectShapesRecursively(root, root, records, true);

        if (records.empty()) {
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
            } else {
                m_rigid_body_index = physics_scene->RegisterRigidBody(root->GetHandle(), BuildPhysicsProperties());
            }
        }

        physics_scene->SetRigidBodyProperties(m_rigid_body_index, BuildPhysicsProperties());

        const Transform root_world = root->GetWorldTransform();
        glm::vec3 center_local_position(0.0f, 0.0f, 0.0f);
        if (!records.empty()) {
            float total_volume = 0.0f;
            glm::vec3 weighted_center(0.0f, 0.0f, 0.0f);
            for (const auto &record : records) {
                const float volume = std::max(record.volume, 0.0f);
                total_volume += volume;
                weighted_center += record.local_center_in_root * volume;
            }

            if (total_volume <= 1e-6f) {
                center_local_position = records.front().local_center_in_root;
            } else {
                center_local_position = weighted_center / total_volume;
            }
        }
        const glm::quat center_local_rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        const glm::vec3 center_world_position =
            root_world.GetPosition() + root_world.GetRotation() * center_local_position;
        const glm::quat center_world_rotation = root_world.GetRotation();

        physics_scene->SetRigidBodyCenterState(
            m_rigid_body_index,
            center_world_position,
            center_world_rotation,
            center_local_position,
            center_local_rotation
        );

        uint32_t shape_start = PhysicsScene::INVALID_INDEX;
        uint32_t shape_count = 0;

        for (auto &record : records) {
            if (record.shape_component == nullptr) {
                continue;
            }

            if (!record.shape_component->IsRegisteredInPhysicsScene()) {
                record.shape_component->Awake();
            }

            record.shape_index = record.shape_component->GetPhysicsShapeIndex();
            if (record.shape_index == PhysicsScene::INVALID_INDEX) {
                continue;
            }

            if (shape_start == PhysicsScene::INVALID_INDEX || record.shape_index < shape_start) {
                shape_start = record.shape_index;
            }
            shape_count++;

            const glm::vec3 local_to_center_pos = record.local_center_in_root - center_local_position;
            const glm::quat local_to_center_rot = glm::normalize(record.local_rotation_in_root);

            physics_scene->SetCollisionShapeRigidBody(record.shape_index, m_rigid_body_index);
            physics_scene->SetCollisionShapeLocalToCenter(record.shape_index, local_to_center_pos, local_to_center_rot);
        }

        physics_scene->SetRigidBodyShapeRange(m_rigid_body_index, shape_start, shape_count);
    }

    uint32_t RigidBodyComponent::GetPhysicsRigidBodyIndex() const noexcept {
        return m_rigid_body_index;
    }

    void RigidBodyComponent::CollectShapesRecursively(
        GameObject *node, GameObject *root, std::vector<ShapeCollectRecord> &records, bool skip_rigidbody_check_on_node
    ) {
        if (node == nullptr || root == nullptr) {
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

        const Transform root_world = root->GetWorldTransform();
        const Transform node_world = node->GetWorldTransform();

        for (ComponentHandle comp_handle : node->m_components) {
            auto *component = scene->GetComponent(comp_handle);
            auto *shape = dynamic_cast<CollisionShapeComponent *>(component);
            if (shape == nullptr) {
                continue;
            }

            ShapeCollectRecord rec{};
            rec.shape_component = shape;
            rec.owner_object = node;

            const glm::vec3 center_world = node_world.GetPosition() + node_world.GetRotation() * shape->m_box_center;
            rec.local_center_in_root =
                glm::inverse(root_world.GetRotation()) * (center_world - root_world.GetPosition());
            rec.local_rotation_in_root = glm::normalize(
                glm::inverse(root_world.GetRotation()) * node_world.GetRotation() * shape->m_box_rotation
            );
            rec.volume = shape->m_box_size.x * shape->m_box_size.y * shape->m_box_size.z;
            records.push_back(rec);
        }

        for (ObjectHandle child_handle : node->GetChildren()) {
            auto *child = scene->GetGameObject(child_handle);
            CollectShapesRecursively(child, root, records, false);
        }
    }

    RigidBodyPhysicsProperties RigidBodyComponent::BuildPhysicsProperties() const {
        RigidBodyPhysicsProperties props{};
        props.m_mass = m_mass;
        props.m_static_friction = m_static_friction;
        props.m_dynamic_friction = m_dynamic_friction;
        props.m_restitution = m_restitution;
        props.m_is_kinematic = m_is_kinematic;
        props.m_linear_velocity = m_linear_velocity;
        props.m_angular_velocity_axis_angle = m_angular_velocity_axis_angle;
        props.m_external_force = m_external_force;
        props.m_external_torque = m_external_torque;
        return props;
    }
} // namespace Engine

#include "__generated__/RigidBodyComponent.h.inc"
