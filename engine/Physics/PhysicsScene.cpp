#include "PhysicsScene.h"

#include <SDL3/SDL.h>

#include <cassert>

namespace Engine {
    PhysicsScene::PhysicsScene(uint32_t scene_id) : m_scene_id(scene_id) {
    }

    PhysicsScene::~PhysicsScene() {
    }

    uint32_t PhysicsScene::GetSceneID() const noexcept {
        return m_scene_id;
    }

    void PhysicsScene::Clear() {
        m_rigid_body_alive.clear();
        m_shape_alive.clear();

        m_rigid_body_to_object.clear();
        m_object_to_rigid_body.clear();

        m_shape_component_to_index.clear();
        m_shape_index_to_component.clear();

        m_rigid_body_properties.clear();
        m_rigid_body_center_world_position.clear();
        m_rigid_body_center_world_rotation.clear();
        m_rigid_body_center_local_position_offset.clear();
        m_rigid_body_center_local_rotation_offset.clear();
        m_rigid_body_linear_velocity.clear();
        m_rigid_body_angular_velocity_axis_angle.clear();
        m_rigid_body_external_force.clear();
        m_rigid_body_external_torque.clear();
        m_rigid_body_shape_start.clear();
        m_rigid_body_shape_count.clear();

        m_shape_to_rigid_body.clear();
        m_shape_type.clear();
        m_shape_box_feature.clear();
        m_shape_world_position.clear();
        m_shape_world_rotation.clear();
        m_shape_local_to_center_position.clear();
        m_shape_local_to_center_rotation.clear();
    }

    uint32_t PhysicsScene::RegisterRigidBody(ObjectHandle owner_object, const RigidBodyPhysicsProperties &props) {
        const uint32_t new_index = static_cast<uint32_t>(m_rigid_body_alive.size());
        m_rigid_body_alive.push_back(true);

        m_rigid_body_to_object.push_back(owner_object);
        m_object_to_rigid_body[owner_object] = new_index;

        m_rigid_body_properties.push_back(props);
        m_rigid_body_center_world_position.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
        m_rigid_body_center_world_rotation.push_back(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        m_rigid_body_center_local_position_offset.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
        m_rigid_body_center_local_rotation_offset.push_back(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        m_rigid_body_linear_velocity.push_back(props.m_linear_velocity);
        m_rigid_body_angular_velocity_axis_angle.push_back(props.m_angular_velocity_axis_angle);
        m_rigid_body_external_force.push_back(props.m_external_force);
        m_rigid_body_external_torque.push_back(props.m_external_torque);
        m_rigid_body_shape_start.push_back(INVALID_INDEX);
        m_rigid_body_shape_count.push_back(0);

        return new_index;
    }

    void PhysicsScene::UnregisterRigidBody(uint32_t rigid_body_index) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            return;
        }

        m_rigid_body_alive[rigid_body_index] = false;
        m_object_to_rigid_body.erase(m_rigid_body_to_object[rigid_body_index]);
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "PhysicsScene::UnregisterRigidBody is not fully implemented yet. scene=%u rigidBody=%u",
            m_scene_id,
            rigid_body_index
        );
    }

    uint32_t PhysicsScene::RegisterCollisionShape(
        ComponentHandle component_handle,
        CollisionShapeType shape_type,
        const CollisionBoxFeature &box_feature,
        const glm::vec3 &world_position,
        const glm::quat &world_rotation
    ) {
        const uint32_t new_index = static_cast<uint32_t>(m_shape_alive.size());
        m_shape_alive.push_back(true);

        m_shape_component_to_index[component_handle] = new_index;
        m_shape_index_to_component.push_back(component_handle);

        m_shape_to_rigid_body.push_back(INVALID_INDEX);
        m_shape_type.push_back(shape_type);
        m_shape_box_feature.push_back(box_feature);
        m_shape_world_position.push_back(world_position);
        m_shape_world_rotation.push_back(world_rotation);
        m_shape_local_to_center_position.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
        m_shape_local_to_center_rotation.push_back(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

        return new_index;
    }

    void PhysicsScene::UnregisterCollisionShape(uint32_t shape_index) {
        if (!IsShapeIndexValid(shape_index)) {
            return;
        }

        m_shape_alive[shape_index] = false;
        m_shape_component_to_index.erase(m_shape_index_to_component[shape_index]);
        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "PhysicsScene::UnregisterCollisionShape is not fully implemented yet. scene=%u shape=%u",
            m_scene_id,
            shape_index
        );
    }

    void PhysicsScene::SetCollisionShapeRigidBody(uint32_t shape_index, uint32_t rigid_body_index) {
        if (!IsShapeIndexValid(shape_index)) {
            return;
        }
        if (rigid_body_index != INVALID_INDEX && !IsRigidBodyIndexValid(rigid_body_index)) {
            return;
        }

        m_shape_to_rigid_body[shape_index] = rigid_body_index;
    }

    void PhysicsScene::SetCollisionShapeLocalToCenter(
        uint32_t shape_index, const glm::vec3 &position, const glm::quat &rotation
    ) {
        if (!IsShapeIndexValid(shape_index)) {
            return;
        }

        m_shape_local_to_center_position[shape_index] = position;
        m_shape_local_to_center_rotation[shape_index] = rotation;
    }

    void PhysicsScene::SetRigidBodyCenterState(
        uint32_t rigid_body_index,
        const glm::vec3 &center_world_position,
        const glm::quat &center_world_rotation,
        const glm::vec3 &center_local_position_offset,
        const glm::quat &center_local_rotation_offset
    ) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            return;
        }

        m_rigid_body_center_world_position[rigid_body_index] = center_world_position;
        m_rigid_body_center_world_rotation[rigid_body_index] = center_world_rotation;
        m_rigid_body_center_local_position_offset[rigid_body_index] = center_local_position_offset;
        m_rigid_body_center_local_rotation_offset[rigid_body_index] = center_local_rotation_offset;
    }

    void PhysicsScene::SetRigidBodyShapeRange(uint32_t rigid_body_index, uint32_t shape_start, uint32_t shape_count) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            return;
        }

        m_rigid_body_shape_start[rigid_body_index] = shape_start;
        m_rigid_body_shape_count[rigid_body_index] = shape_count;
    }

    void PhysicsScene::SetRigidBodyProperties(uint32_t rigid_body_index, const RigidBodyPhysicsProperties &props) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            return;
        }

        m_rigid_body_properties[rigid_body_index] = props;
        m_rigid_body_linear_velocity[rigid_body_index] = props.m_linear_velocity;
        m_rigid_body_angular_velocity_axis_angle[rigid_body_index] = props.m_angular_velocity_axis_angle;
        m_rigid_body_external_force[rigid_body_index] = props.m_external_force;
        m_rigid_body_external_torque[rigid_body_index] = props.m_external_torque;
    }

    bool PhysicsScene::IsRigidBodyIndexValid(uint32_t rigid_body_index) const {
        return rigid_body_index < m_rigid_body_alive.size() && m_rigid_body_alive[rigid_body_index];
    }

    bool PhysicsScene::IsShapeIndexValid(uint32_t shape_index) const {
        return shape_index < m_shape_alive.size() && m_shape_alive[shape_index];
    }

    uint32_t PhysicsScene::FindRigidBodyByObjectHandle(ObjectHandle object_handle) const {
        auto iter = m_object_to_rigid_body.find(object_handle);
        if (iter == m_object_to_rigid_body.end()) {
            return INVALID_INDEX;
        }
        return iter->second;
    }

    uint32_t PhysicsScene::FindShapeByComponentHandle(ComponentHandle component_handle) const {
        auto iter = m_shape_component_to_index.find(component_handle);
        if (iter == m_shape_component_to_index.end()) {
            return INVALID_INDEX;
        }
        return iter->second;
    }

    void PhysicsScene::DebugPrint() const {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "PhysicsScene %u:", m_scene_id);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "  Rigid bodies:");
        for (size_t i = 0; i < m_rigid_body_alive.size(); i++) {
            if (!m_rigid_body_alive[i]) {
                continue;
            }
            SDL_LogInfo(
                SDL_LOG_CATEGORY_APPLICATION,
                "    [%02u] object=%02u properties={mass=%.2f, static_friction=%.2f, dynamic_friction=%.2f, "
                "restitution=%.2f, is_kinematic=%s\n"
                "       pos=(%.2f, %.2f, %.2f) rot=(%.2f, %.2f, %.2f, %.2f)\n"
                "       local_pos_offset=(%.2f, %.2f, %.2f) local_rot_offset=(%.2f, %.2f, %.2f, %.2f)}",
                static_cast<unsigned int>(i),
                m_rigid_body_to_object[i].GetID(),
                m_rigid_body_properties[i].m_mass,
                m_rigid_body_properties[i].m_static_friction,
                m_rigid_body_properties[i].m_dynamic_friction,
                m_rigid_body_properties[i].m_restitution,
                m_rigid_body_properties[i].m_is_kinematic ? "true" : "false",
                m_rigid_body_center_world_position[i].x,
                m_rigid_body_center_world_position[i].y,
                m_rigid_body_center_world_position[i].z,
                m_rigid_body_center_world_rotation[i].x,
                m_rigid_body_center_world_rotation[i].y,
                m_rigid_body_center_world_rotation[i].z,
                m_rigid_body_center_world_rotation[i].w,
                m_rigid_body_center_local_position_offset[i].x,
                m_rigid_body_center_local_position_offset[i].y,
                m_rigid_body_center_local_position_offset[i].z,
                m_rigid_body_center_local_rotation_offset[i].x,
                m_rigid_body_center_local_rotation_offset[i].y,
                m_rigid_body_center_local_rotation_offset[i].z,
                m_rigid_body_center_local_rotation_offset[i].w
            );
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "  Collision shapes:");
        for (size_t i = 0; i < m_shape_alive.size(); i++) {
            if (!m_shape_alive[i]) {
                continue;
            }
            SDL_LogInfo(
                SDL_LOG_CATEGORY_APPLICATION,
                "    [%02u] component=%02u type=%d belong_to_rigidbody=%d\n"
                "           box_feature={size=(%.2f, %.2f, %.2f), center=(%.2f, %.2f, %.2f), "
                "rotation=(%.2f, %.2f, %.2f, %.2f)}",
                static_cast<unsigned int>(i),
                m_shape_index_to_component[i].GetID(),
                static_cast<int>(m_shape_type[i]),
                m_shape_to_rigid_body[i],
                m_shape_box_feature[i].m_half_extents.x,
                m_shape_box_feature[i].m_half_extents.y,
                m_shape_box_feature[i].m_half_extents.z,
                m_shape_box_feature[i].m_center.x,
                m_shape_box_feature[i].m_center.y,
                m_shape_box_feature[i].m_center.z,
                m_shape_box_feature[i].m_rotation.x,
                m_shape_box_feature[i].m_rotation.y,
                m_shape_box_feature[i].m_rotation.z,
                m_shape_box_feature[i].m_rotation.w
            );
        }
    }
} // namespace Engine

#include "__generated__/PhysicsScene.h.inc"
