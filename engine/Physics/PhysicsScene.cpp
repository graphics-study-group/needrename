#include "PhysicsScene.h"

#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>

#include <SDL3/SDL.h>

#include <algorithm>
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

        m_rigid_body_mass.clear();
        m_rigid_body_static_friction.clear();
        m_rigid_body_dynamic_friction.clear();
        m_rigid_body_restitution.clear();
        m_rigid_body_is_kinematic.clear();
        m_rigid_body_center_world_position.clear();
        m_rigid_body_center_world_rotation.clear();
        m_rigid_body_center_offset_local_position.clear();
        m_rigid_body_inertia_tensor_local.clear();
        m_rigid_body_linear_velocity.clear();
        m_rigid_body_angular_velocity_axis_angle.clear();
        m_rigid_body_external_force.clear();
        m_rigid_body_external_torque.clear();
        m_rigid_body_need_init.clear();
        m_rigid_body_init_queue.clear();
        m_rigid_body_to_shapes.clear();

        m_shape_to_rigid_body.clear();
        m_shape_type.clear();
        m_shape_half_extents.clear();
        m_shape_position.clear();
        m_shape_rotation.clear();
        m_shape_world_position.clear();
        m_shape_world_rotation.clear();
    }

    uint32_t PhysicsScene::RegisterRigidBody(
        ObjectHandle owner_object,
        float mass,
        float static_friction,
        float dynamic_friction,
        float restitution,
        bool is_kinematic,
        const glm::vec3 &linear_velocity,
        const glm::vec3 &angular_velocity_axis_angle,
        const glm::vec3 &external_force,
        const glm::vec3 &external_torque
    ) {
        const uint32_t new_index = static_cast<uint32_t>(m_rigid_body_alive.size());
        m_rigid_body_alive.push_back(true);

        m_rigid_body_to_object.push_back(owner_object);
        m_object_to_rigid_body[owner_object] = new_index;

        m_rigid_body_mass.push_back(mass);
        m_rigid_body_static_friction.push_back(static_friction);
        m_rigid_body_dynamic_friction.push_back(dynamic_friction);
        m_rigid_body_restitution.push_back(restitution);
        m_rigid_body_is_kinematic.push_back(is_kinematic);
        m_rigid_body_center_world_position.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
        m_rigid_body_center_world_rotation.push_back(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        m_rigid_body_center_offset_local_position.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
        m_rigid_body_inertia_tensor_local.push_back(glm::mat3(0.0f));
        m_rigid_body_linear_velocity.push_back(linear_velocity);
        m_rigid_body_angular_velocity_axis_angle.push_back(angular_velocity_axis_angle);
        m_rigid_body_external_force.push_back(external_force);
        m_rigid_body_external_torque.push_back(external_torque);
        m_rigid_body_need_init.push_back(false);
        m_rigid_body_to_shapes[new_index] = {};

        EnqueueRigidBodyInitialization(new_index);

        return new_index;
    }

    void PhysicsScene::UnregisterRigidBody(uint32_t rigid_body_index) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            return;
        }

        const auto map_iter = m_rigid_body_to_shapes.find(rigid_body_index);
        if (map_iter != m_rigid_body_to_shapes.end()) {
            for (uint32_t shape_index : map_iter->second) {
                if (shape_index < m_shape_to_rigid_body.size()
                    && m_shape_to_rigid_body[shape_index] == rigid_body_index) {
                    m_shape_to_rigid_body[shape_index] = INVALID_INDEX;
                    if (shape_index < m_shape_position.size() && shape_index < m_shape_world_position.size()) {
                        m_shape_position[shape_index] = m_shape_world_position[shape_index];
                        m_shape_rotation[shape_index] = m_shape_world_rotation[shape_index];
                    }
                }
            }
        }
        m_rigid_body_to_shapes.erase(rigid_body_index);

        if (rigid_body_index < m_rigid_body_need_init.size()) {
            m_rigid_body_need_init[rigid_body_index] = false;
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
        const glm::vec3 &half_extents,
        const glm::vec3 &shape_world_position,
        const glm::quat &shape_world_rotation
    ) {
        const uint32_t new_index = static_cast<uint32_t>(m_shape_alive.size());
        m_shape_alive.push_back(true);

        m_shape_component_to_index[component_handle] = new_index;
        m_shape_index_to_component.push_back(component_handle);

        m_shape_to_rigid_body.push_back(INVALID_INDEX);
        m_shape_type.push_back(shape_type);
        m_shape_half_extents.push_back(half_extents);
        m_shape_position.push_back(shape_world_position);
        m_shape_rotation.push_back(shape_world_rotation);
        m_shape_world_position.push_back(shape_world_position);
        m_shape_world_rotation.push_back(shape_world_rotation);

        return new_index;
    }

    void PhysicsScene::UnregisterCollisionShape(uint32_t shape_index) {
        if (!IsShapeIndexValid(shape_index)) {
            return;
        }

        const uint32_t old_rigid_body = m_shape_to_rigid_body[shape_index];
        if (old_rigid_body != INVALID_INDEX) {
            RemoveShapeFromRigidBodyMap(old_rigid_body, shape_index);
            EnqueueRigidBodyInitialization(old_rigid_body);
        }
        m_shape_to_rigid_body[shape_index] = INVALID_INDEX;
        m_shape_position[shape_index] = m_shape_world_position[shape_index];
        m_shape_rotation[shape_index] = m_shape_world_rotation[shape_index];

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

        const uint32_t old_rigid_body_index = m_shape_to_rigid_body[shape_index];
        if (old_rigid_body_index == rigid_body_index) {
            return;
        }

        if (old_rigid_body_index != INVALID_INDEX) {
            RemoveShapeFromRigidBodyMap(old_rigid_body_index, shape_index);
            EnqueueRigidBodyInitialization(old_rigid_body_index);
        }

        m_shape_to_rigid_body[shape_index] = rigid_body_index;
        if (rigid_body_index != INVALID_INDEX) {
            AddShapeToRigidBodyMap(rigid_body_index, shape_index);
            EnqueueRigidBodyInitialization(rigid_body_index);
        } else {
            m_shape_position[shape_index] = m_shape_world_position[shape_index];
            m_shape_rotation[shape_index] = m_shape_world_rotation[shape_index];
        }
    }

    void PhysicsScene::UpdateCollisionShapeGeometry(
        uint32_t shape_index,
        CollisionShapeType shape_type,
        const glm::vec3 &half_extents,
        const glm::vec3 &shape_world_position,
        const glm::quat &shape_world_rotation
    ) {
        if (!IsShapeIndexValid(shape_index)) {
            return;
        }

        m_shape_type[shape_index] = shape_type;
        m_shape_half_extents[shape_index] = half_extents;
        m_shape_world_position[shape_index] = shape_world_position;
        m_shape_world_rotation[shape_index] = glm::normalize(shape_world_rotation);
        const uint32_t rigid_body_index = m_shape_to_rigid_body[shape_index];
        if (rigid_body_index == INVALID_INDEX) {
            m_shape_position[shape_index] = shape_world_position;
            m_shape_rotation[shape_index] = glm::normalize(shape_world_rotation);
        } else {
            EnqueueRigidBodyInitialization(rigid_body_index);
        }
    }

    void PhysicsScene::SetRigidBodyProperties(
        uint32_t rigid_body_index,
        float mass,
        float static_friction,
        float dynamic_friction,
        float restitution,
        bool is_kinematic,
        const glm::vec3 &linear_velocity,
        const glm::vec3 &angular_velocity_axis_angle,
        const glm::vec3 &external_force,
        const glm::vec3 &external_torque
    ) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            return;
        }

        m_rigid_body_mass[rigid_body_index] = mass;
        m_rigid_body_static_friction[rigid_body_index] = static_friction;
        m_rigid_body_dynamic_friction[rigid_body_index] = dynamic_friction;
        m_rigid_body_restitution[rigid_body_index] = restitution;
        m_rigid_body_is_kinematic[rigid_body_index] = is_kinematic;
        m_rigid_body_linear_velocity[rigid_body_index] = linear_velocity;
        m_rigid_body_angular_velocity_axis_angle[rigid_body_index] = angular_velocity_axis_angle;
        m_rigid_body_external_force[rigid_body_index] = external_force;
        m_rigid_body_external_torque[rigid_body_index] = external_torque;

        EnqueueRigidBodyInitialization(rigid_body_index);
    }

    void PhysicsScene::EnqueueRigidBodyInitialization(uint32_t rigid_body_index) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            return;
        }
        if (m_rigid_body_need_init[rigid_body_index]) {
            return;
        }
        m_rigid_body_need_init[rigid_body_index] = true;
        m_rigid_body_init_queue.push_back(rigid_body_index);
    }

    void PhysicsScene::InitializePendingRigidBodies() {
        while (!m_rigid_body_init_queue.empty()) {
            const uint32_t rigid_body_index = m_rigid_body_init_queue.front();
            m_rigid_body_init_queue.pop_front();

            if (!IsRigidBodyIndexValid(rigid_body_index)) {
                continue;
            }
            if (!m_rigid_body_need_init[rigid_body_index]) {
                continue;
            }

            RecalculateRigidBodyState(rigid_body_index);
            m_rigid_body_need_init[rigid_body_index] = false;
        }
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

    void PhysicsScene::AddShapeToRigidBodyMap(uint32_t rigid_body_index, uint32_t shape_index) {
        auto &shape_list = m_rigid_body_to_shapes[rigid_body_index];
        if (std::find(shape_list.begin(), shape_list.end(), shape_index) == shape_list.end()) {
            shape_list.push_back(shape_index);
        }
    }

    void PhysicsScene::RemoveShapeFromRigidBodyMap(uint32_t rigid_body_index, uint32_t shape_index) {
        auto iter = m_rigid_body_to_shapes.find(rigid_body_index);
        if (iter == m_rigid_body_to_shapes.end()) {
            return;
        }

        auto &shape_list = iter->second;
        shape_list.erase(std::remove(shape_list.begin(), shape_list.end(), shape_index), shape_list.end());
    }

    void PhysicsScene::RecalculateRigidBodyState(uint32_t rigid_body_index) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            return;
        }

        glm::vec3 object_world_position = m_rigid_body_center_world_position[rigid_body_index]
                                          - m_rigid_body_center_offset_local_position[rigid_body_index];
        glm::quat object_world_rotation = m_rigid_body_center_world_rotation[rigid_body_index];

        auto map_iter = m_rigid_body_to_shapes.find(rigid_body_index);
        if (map_iter == m_rigid_body_to_shapes.end() || map_iter->second.empty()) {
            m_rigid_body_center_world_position[rigid_body_index] = object_world_position;
            m_rigid_body_center_world_rotation[rigid_body_index] = object_world_rotation;
            m_rigid_body_center_offset_local_position[rigid_body_index] = glm::vec3(0.0f, 0.0f, 0.0f);
            m_rigid_body_inertia_tensor_local[rigid_body_index] = glm::mat3(0.0f);
            return;
        }

        std::vector<uint32_t> valid_shape_indices;
        valid_shape_indices.reserve(map_iter->second.size());
        float total_volume = 0.0f;
        glm::vec3 weighted_center_world(0.0f, 0.0f, 0.0f);

        for (uint32_t shape_index : map_iter->second) {
            if (!IsShapeIndexValid(shape_index)) {
                continue;
            }
            if (m_shape_to_rigid_body[shape_index] != rigid_body_index) {
                continue;
            }

            valid_shape_indices.push_back(shape_index);

            const glm::vec3 half_extents = glm::abs(m_shape_half_extents[shape_index]);
            const float volume = 8.0f * half_extents.x * half_extents.y * half_extents.z;
            total_volume += volume;
            weighted_center_world += m_shape_world_position[shape_index] * volume;
        }

        if (valid_shape_indices.empty()) {
            m_rigid_body_center_world_position[rigid_body_index] = object_world_position;
            m_rigid_body_center_world_rotation[rigid_body_index] = object_world_rotation;
            m_rigid_body_center_offset_local_position[rigid_body_index] = glm::vec3(0.0f, 0.0f, 0.0f);
            m_rigid_body_inertia_tensor_local[rigid_body_index] = glm::mat3(0.0f);
            return;
        }

        glm::vec3 center_world_position = weighted_center_world / total_volume;
        const glm::quat center_world_rotation = object_world_rotation;
        const glm::quat inv_center_world_rotation = glm::inverse(center_world_rotation);
        const glm::vec3 center_offset_local_position =
            inv_center_world_rotation * (center_world_position - object_world_position);

        m_rigid_body_center_world_position[rigid_body_index] = center_world_position;
        m_rigid_body_center_world_rotation[rigid_body_index] = center_world_rotation;
        m_rigid_body_center_offset_local_position[rigid_body_index] = center_offset_local_position;

        glm::mat3 inertia_tensor(0.0f);
        const float total_mass = std::max(m_rigid_body_mass[rigid_body_index], 0.0f);
        const float fallback_mass =
            valid_shape_indices.empty() ? 0.0f : (total_mass / static_cast<float>(valid_shape_indices.size()));

        for (uint32_t shape_index : valid_shape_indices) {
            const glm::vec3 half_extents = glm::abs(m_shape_half_extents[shape_index]);
            const float volume = 8.0f * half_extents.x * half_extents.y * half_extents.z;
            const float mass = total_volume > 1e-6f ? (total_mass * (volume / total_volume)) : fallback_mass;

            const glm::vec3 shape_world_position = m_shape_world_position[shape_index];
            const glm::quat shape_world_rotation = glm::normalize(m_shape_world_rotation[shape_index]);

            const glm::vec3 shape_local_position =
                inv_center_world_rotation * (shape_world_position - center_world_position);
            const glm::quat shape_local_rotation = glm::normalize(inv_center_world_rotation * shape_world_rotation);

            m_shape_position[shape_index] = shape_local_position;
            m_shape_rotation[shape_index] = shape_local_rotation;

            const float hx = half_extents.x;
            const float hy = half_extents.y;
            const float hz = half_extents.z;
            glm::mat3 inertia_shape(0.0f);
            inertia_shape[0][0] = (mass / 3.0f) * (hy * hy + hz * hz);
            inertia_shape[1][1] = (mass / 3.0f) * (hx * hx + hz * hz);
            inertia_shape[2][2] = (mass / 3.0f) * (hx * hx + hy * hy);

            const glm::mat3 rotation_matrix = glm::mat3_cast(shape_local_rotation);
            const glm::mat3 rotated_inertia = rotation_matrix * inertia_shape * glm::transpose(rotation_matrix);

            const glm::vec3 d = shape_local_position;
            const float d2 = glm::dot(d, d);
            const glm::mat3 identity(1.0f);
            const glm::mat3 parallel_axis = mass * ((d2 * identity) - glm::outerProduct(d, d));

            inertia_tensor += rotated_inertia + parallel_axis;
        }

        m_rigid_body_inertia_tensor_local[rigid_body_index] = inertia_tensor;
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
                "    [%02u] object=%02u props={mass=%.2f, static_friction=%.2f, dynamic_friction=%.2f, "
                "restitution=%.2f, is_kinematic=%s}\n"
                "       center_world_pos=(%.2f, %.2f, %.2f) center_world_rot=(%.2f, %.2f, %.2f, %.2f)\n"
                "       center_offset_local_pos=(%.2f, %.2f, %.2f)\n"
                "       inertia_local=[[%.2f, %.2f, %.2f], [%.2f, %.2f, %.2f], [%.2f, %.2f, %.2f]] need_init=%s\n"
                "       linear_velocity=(%.2f, %.2f, %.2f) angular_velocity_axis_angle=(%.2f, %.2f, %.2f)\n"
                "       external_force=(%.2f, %.2f, %.2f) external_torque=(%.2f, %.2f, %.2f)",
                static_cast<unsigned int>(i),
                m_rigid_body_to_object[i].GetID(),
                m_rigid_body_mass[i],
                m_rigid_body_static_friction[i],
                m_rigid_body_dynamic_friction[i],
                m_rigid_body_restitution[i],
                m_rigid_body_is_kinematic[i] ? "true" : "false",
                m_rigid_body_center_world_position[i].x,
                m_rigid_body_center_world_position[i].y,
                m_rigid_body_center_world_position[i].z,
                m_rigid_body_center_world_rotation[i].x,
                m_rigid_body_center_world_rotation[i].y,
                m_rigid_body_center_world_rotation[i].z,
                m_rigid_body_center_world_rotation[i].w,
                m_rigid_body_center_offset_local_position[i].x,
                m_rigid_body_center_offset_local_position[i].y,
                m_rigid_body_center_offset_local_position[i].z,
                m_rigid_body_inertia_tensor_local[i][0][0],
                m_rigid_body_inertia_tensor_local[i][0][1],
                m_rigid_body_inertia_tensor_local[i][0][2],
                m_rigid_body_inertia_tensor_local[i][1][0],
                m_rigid_body_inertia_tensor_local[i][1][1],
                m_rigid_body_inertia_tensor_local[i][1][2],
                m_rigid_body_inertia_tensor_local[i][2][0],
                m_rigid_body_inertia_tensor_local[i][2][1],
                m_rigid_body_inertia_tensor_local[i][2][2],
                (i < m_rigid_body_need_init.size() && m_rigid_body_need_init[i]) ? "true" : "false",
                m_rigid_body_linear_velocity[i].x,
                m_rigid_body_linear_velocity[i].y,
                m_rigid_body_linear_velocity[i].z,
                m_rigid_body_angular_velocity_axis_angle[i].x,
                m_rigid_body_angular_velocity_axis_angle[i].y,
                m_rigid_body_angular_velocity_axis_angle[i].z,
                m_rigid_body_external_force[i].x,
                m_rigid_body_external_force[i].y,
                m_rigid_body_external_force[i].z,
                m_rigid_body_external_torque[i].x,
                m_rigid_body_external_torque[i].y,
                m_rigid_body_external_torque[i].z
            );
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "  Collision shapes:");
        for (size_t i = 0; i < m_shape_alive.size(); i++) {
            if (!m_shape_alive[i]) {
                continue;
            }
            const bool bound = m_shape_to_rigid_body[i] != INVALID_INDEX;
            const char *mode = bound ? "local_to_com" : "world";
            SDL_LogInfo(
                SDL_LOG_CATEGORY_APPLICATION,
                "    [%02u] component=%02u type=%d bound_rigidbody=%d mode=%s\n"
                "           half_extents=(%.2f, %.2f, %.2f) pose_pos=(%.2f, %.2f, %.2f) "
                "pose_rot=(%.2f, %.2f, %.2f, %.2f)",
                static_cast<unsigned int>(i),
                m_shape_index_to_component[i].GetID(),
                static_cast<int>(m_shape_type[i]),
                m_shape_to_rigid_body[i],
                mode,
                m_shape_half_extents[i].x,
                m_shape_half_extents[i].y,
                m_shape_half_extents[i].z,
                m_shape_position[i].x,
                m_shape_position[i].y,
                m_shape_position[i].z,
                m_shape_rotation[i].x,
                m_shape_rotation[i].y,
                m_shape_rotation[i].z,
                m_shape_rotation[i].w
            );
        }
    }
} // namespace Engine

#include "__generated__/PhysicsScene.h.inc"
