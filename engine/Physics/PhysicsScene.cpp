#include "PhysicsScene.h"

#include <Framework/object/GameObject.h>
#include <Framework/world/Scene.h>
#include <Render/Memory/ComputeBuffer.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/FrameManager.h>
#include <vulkan/vulkan.hpp>

#include <Render/RenderSystem/SceneDataManager.h>
#include <Render/RenderSystem/SubmissionHelper.h>

#include <SDL3/SDL.h>

#include <Framework/component/physics/CollisionShapeComponent.h>

#include <algorithm>
#include <cassert>
#include <span>

namespace {
    template <typename T>
    std::span<const std::byte> MakeSpan(const std::vector<T> &source) {
        if (source.empty()) {
            static const T kSentinel{};
            return std::as_bytes(std::span{&kSentinel, 1});
        }
        return std::as_bytes(std::span{source});
    }

    template <typename T>
    void EnsureBuffer(
        std::unique_ptr<Engine::ComputeBuffer> &buffer,
        const Engine::RenderSystemState::AllocatorState &allocator,
        size_t element_count,
        const std::string &name
    ) {
        const size_t safe_count = std::max<size_t>(1, element_count);
        const size_t byte_size = safe_count * sizeof(T);
        if (!buffer || buffer->GetSize() != byte_size) {
            buffer = Engine::ComputeBuffer::CreateUnique(allocator, byte_size, false, false, false, false, name);
        }
    }

    glm::vec4 ToVec4(const glm::vec3 &v) {
        return glm::vec4(v.x, v.y, v.z, 0.0f);
    }

    glm::vec4 ToVec4(const glm::quat &q) {
        return glm::vec4(q.x, q.y, q.z, q.w);
    }

    glm::vec3 Vec4ToVec3(const glm::vec4 &v) {
        return glm::vec3(v.x, v.y, v.z);
    }

    glm::quat Vec4ToQuat(const glm::vec4 &v) {
        return glm::quat(v.w, v.x, v.y, v.z);
    }

    // -------------------------------------------------------------------------
    // Per-shape volume and inertia helpers
    // -------------------------------------------------------------------------

    float ComputeBoxVolume(const glm::vec3 &feature) {
        return 8.0f * feature.x * feature.y * feature.z;
    }

    float ComputeSphereVolume(float radius) {
        return (4.0f / 3.0f) * glm::pi<float>() * radius * radius * radius;
    }

    float ComputeCylinderVolume(float radius, float half_height) {
        return glm::pi<float>() * radius * radius * 2.0f * half_height;
    }

    glm::mat3 ComputeBoxInertia(float mass, const glm::vec3 &feature) {
        const float hx = feature.x;
        const float hy = feature.y;
        const float hz = feature.z;
        glm::mat3 inertia(0.0f);
        inertia[0][0] = (mass / 3.0f) * (hy * hy + hz * hz);
        inertia[1][1] = (mass / 3.0f) * (hx * hx + hz * hz);
        inertia[2][2] = (mass / 3.0f) * (hx * hx + hy * hy);
        return inertia;
    }

    glm::mat3 ComputeSphereInertia(float mass, float radius) {
        const float diag = (2.0f / 5.0f) * mass * radius * radius;
        glm::mat3 inertia(0.0f);
        inertia[0][0] = diag;
        inertia[1][1] = diag;
        inertia[2][2] = diag;
        return inertia;
    }

    glm::mat3 ComputeCylinderInertia(float mass, float radius, float half_height) {
        const float h = 2.0f * half_height;
        const float r2 = radius * radius;
        const float h2 = h * h;
        glm::mat3 inertia(0.0f);
        // Z-up: axial moment about Z, transverse about X and Y
        inertia[0][0] = (mass / 12.0f) * (3.0f * r2 + h2);
        inertia[1][1] = (mass / 12.0f) * (3.0f * r2 + h2);
        inertia[2][2] = (mass / 2.0f) * r2;
        return inertia;
    }

    // -------------------------------------------------------------------------
    // Collision filter cleanup helper
    // -------------------------------------------------------------------------

    void CleanupShapeFilterReferences(
        uint32_t removed_shape_index,
        size_t shape_count,
        std::vector<std::vector<Engine::ObjectHandle>> &pending_filter_handles,
        std::vector<uint32_t> &shape_filter_offset,
        std::vector<uint32_t> &shape_filter_count,
        std::vector<uint32_t> &shape_filter_data
    ) {
        // Clean pending handles for this shape.
        if (removed_shape_index < pending_filter_handles.size()) {
            pending_filter_handles[removed_shape_index].clear();
        }

        // Clean resolved filter data.
        if (!shape_filter_data.empty()) {
            // Extract all existing filter lists, excluding references to the removed shape.
            std::vector<std::vector<uint32_t>> all_filters(shape_count);
            for (size_t i = 0; i < shape_count && i < shape_filter_offset.size() && i < shape_filter_count.size();
                 ++i) {
                if (shape_filter_count[i] == 0u) {
                    continue;
                }
                const uint32_t offset = shape_filter_offset[i];
                const uint32_t count = shape_filter_count[i];
                auto &out = all_filters[i];
                out.reserve(count);
                for (uint32_t j = 0; j < count && (offset + j) < shape_filter_data.size(); ++j) {
                    const uint32_t val = shape_filter_data[offset + j];
                    if (val != removed_shape_index) {
                        out.push_back(val);
                    }
                }
            }

            // Rebuild flat arrays.
            shape_filter_offset.resize(shape_count);
            shape_filter_count.resize(shape_count);
            shape_filter_data.clear();

            size_t total = 0;
            for (const auto &list : all_filters) {
                total += list.size();
            }
            shape_filter_data.reserve(total);

            for (size_t i = 0; i < shape_count; ++i) {
                shape_filter_offset[i] = static_cast<uint32_t>(shape_filter_data.size());
                shape_filter_count[i] = static_cast<uint32_t>(all_filters[i].size());
                shape_filter_data.insert(shape_filter_data.end(), all_filters[i].begin(), all_filters[i].end());
            }
        }
    }
} // namespace

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
        m_rigid_body_inertia.clear();
        m_rigid_body_inverse_inertia.clear();
        m_rigid_body_linear_velocity.clear();
        m_rigid_body_angular_velocity.clear();
        m_rigid_body_external_force.clear();
        m_rigid_body_external_torque.clear();
        m_rigid_body_need_init.clear();
        m_rigid_body_init_queue.clear();
        m_rigid_body_to_shapes.clear();

        m_shape_to_rigid_body.clear();
        m_shape_type.clear();
        m_shape_feature.clear();
        m_shape_position.clear();
        m_shape_rotation.clear();
        m_shape_world_position.clear();
        m_shape_world_rotation.clear();

        m_gpu_rigid_body_slot_count = 0;
        m_gpu_shape_slot_count = 0;
        m_gpu_flattened_shape_index_count = 0;

        m_fixed_joints.clear();
        m_hinge_joints.clear();
        m_gpu_fixed_joints.reset();
        m_gpu_hinge_joints.reset();

        m_gpu_rigid_body_alive.reset();
        m_gpu_rigid_body_mass.reset();
        m_gpu_rigid_body_static_friction.reset();
        m_gpu_rigid_body_dynamic_friction.reset();
        m_gpu_rigid_body_restitution.reset();
        m_gpu_rigid_body_is_kinematic.reset();
        m_gpu_rigid_body_center_world_position.reset();
        m_gpu_rigid_body_center_world_rotation.reset();
        m_gpu_rigid_body_center_offset_local_position.reset();
        m_gpu_rigid_body_inertia.reset();
        m_gpu_rigid_body_inverse_inertia.reset();
        m_gpu_rigid_body_linear_velocity.reset();
        m_gpu_rigid_body_angular_velocity.reset();
        m_gpu_rigid_body_external_force.reset();
        m_gpu_rigid_body_external_torque.reset();

        m_gpu_shape_alive.reset();
        m_gpu_shape_type.reset();
        m_gpu_shape_bound_rigid_body.reset();
        m_gpu_shape_feature.reset();
        m_gpu_shape_local_position.reset();
        m_gpu_shape_local_rotation.reset();
        m_gpu_shape_world_position.reset();
        m_gpu_shape_world_rotation.reset();

        m_gpu_rigid_body_shape_offset.reset();
        m_gpu_rigid_body_shape_count.reset();
        m_gpu_flattened_shape_indices.reset();

        m_gpu_model_matrices.reset();
    }

    uint32_t PhysicsScene::RegisterRigidBody(
        ObjectHandle owner_object,
        float mass,
        float static_friction,
        float dynamic_friction,
        float restitution,
        bool is_kinematic,
        const glm::vec3 &initial_world_position,
        const glm::quat &initial_world_rotation,
        const glm::vec3 &linear_velocity,
        const glm::vec3 &angular_velocity_axis_angle,
        const glm::vec3 &external_force,
        const glm::vec3 &external_torque
    ) {
        const uint32_t new_index = static_cast<uint32_t>(m_rigid_body_alive.size());
        m_rigid_body_alive.push_back(1u);

        m_rigid_body_to_object.push_back(owner_object);
        m_object_to_rigid_body[owner_object] = new_index;

        m_rigid_body_mass.push_back(mass);
        m_rigid_body_static_friction.push_back(static_friction);
        m_rigid_body_dynamic_friction.push_back(dynamic_friction);
        m_rigid_body_restitution.push_back(restitution);
        m_rigid_body_is_kinematic.push_back(is_kinematic ? 1u : 0u);
        m_rigid_body_center_world_position.push_back(glm::vec4(initial_world_position, 0.0f));
        m_rigid_body_center_world_rotation.push_back(
            glm::vec4(
                initial_world_rotation.x, initial_world_rotation.y, initial_world_rotation.z, initial_world_rotation.w
            )
        );
        m_rigid_body_center_offset_local_position.push_back(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
        m_rigid_body_inertia.push_back(glm::mat4(0.0f));
        m_rigid_body_inverse_inertia.push_back(glm::mat4(0.0f));
        m_rigid_body_linear_velocity.push_back(ToVec4(linear_velocity));
        m_rigid_body_angular_velocity.push_back(ToVec4(angular_velocity_axis_angle));
        m_rigid_body_external_force.push_back(ToVec4(external_force));
        m_rigid_body_external_torque.push_back(ToVec4(external_torque));
        m_rigid_body_need_init.push_back(false);
        m_rigid_body_use_manual_inertia.push_back(false);
        m_rigid_body_manual_inertia.push_back(glm::mat3(0.0f));
        m_rigid_body_to_shapes[new_index] = {};
        m_rigid_body_model_matrix_active.push_back(false);

        EnqueueRigidBodyInitialization(new_index);

        return new_index;
    }

    void PhysicsScene::SetRigidBodyManualInertia(uint32_t rigid_body_index, const glm::mat3 &inertia) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            return;
        }
        if (rigid_body_index >= m_rigid_body_use_manual_inertia.size()) {
            return;
        }
        m_rigid_body_use_manual_inertia[rigid_body_index] = true;
        m_rigid_body_manual_inertia[rigid_body_index] = inertia;
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

        m_rigid_body_alive[rigid_body_index] = 0u;
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
        const glm::vec3 &feature,
        const glm::vec3 &shape_world_position,
        const glm::quat &shape_world_rotation,
        const std::vector<ObjectHandle> &ignore_collision_objects
    ) {
        const uint32_t new_index = static_cast<uint32_t>(m_shape_alive.size());
        m_shape_alive.push_back(1u);

        m_shape_component_to_index[component_handle] = new_index;
        m_shape_index_to_component.push_back(component_handle);

        m_shape_to_rigid_body.push_back(INVALID_INDEX);
        m_shape_type.push_back(static_cast<uint32_t>(shape_type));
        m_shape_feature.push_back(ToVec4(feature));
        m_shape_position.push_back(ToVec4(shape_world_position));
        m_shape_rotation.push_back(ToVec4(shape_world_rotation));
        m_shape_world_position.push_back(ToVec4(shape_world_position));
        m_shape_world_rotation.push_back(ToVec4(shape_world_rotation));

        // Resize pending filter handles to match shape slot count, then store.
        m_pending_filter_handles.resize(m_shape_alive.size());
        m_pending_filter_handles[new_index] = ignore_collision_objects;

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

        m_shape_alive[shape_index] = 0u;
        m_shape_component_to_index.erase(m_shape_index_to_component[shape_index]);

        // Remove references to this shape from other shapes' filter data.
        CleanupShapeFilterReferences(
            shape_index,
            m_shape_alive.size(),
            m_pending_filter_handles,
            m_shape_filter_offset,
            m_shape_filter_count,
            m_shape_filter_data
        );

        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "PhysicsScene::UnregisterCollisionShape is not fully implemented yet. scene=%u shape=%u",
            m_scene_id,
            shape_index
        );
    }

    void PhysicsScene::RegisterFixedJoint(
        uint32_t obj1_index,
        uint32_t obj2_index,
        float compliance,
        const glm::vec3 &initial_rel_pos_local,
        const glm::quat &initial_rel_rotation
    ) {
        GpuFixedJoint joint{};
        joint.obj1_index = obj1_index;
        joint.obj2_index = obj2_index;
        joint.compliance = compliance;
        joint._pad = 0.0f;
        joint.initial_rel_pos_local =
            glm::vec4(initial_rel_pos_local.x, initial_rel_pos_local.y, initial_rel_pos_local.z, 0.0f);
        joint.initial_rel_rotation =
            glm::vec4(initial_rel_rotation.x, initial_rel_rotation.y, initial_rel_rotation.z, initial_rel_rotation.w);
        m_fixed_joints.push_back(joint);
    }

    void PhysicsScene::RegisterHingeJoint(
        uint32_t obj1_index,
        uint32_t obj2_index,
        float compliance,
        const glm::vec3 &hinge_axis_obj1,
        const glm::vec3 &hinge_anchor_obj1,
        const glm::vec3 &initial_rel_pos_local,
        const glm::quat &initial_rel_rotation
    ) {
        GpuHingeJoint joint{};
        joint.obj1_index = obj1_index;
        joint.obj2_index = obj2_index;
        joint.compliance = compliance;
        joint._pad = 0.0f;
        joint.hinge_axis_obj1 = glm::vec4(hinge_axis_obj1.x, hinge_axis_obj1.y, hinge_axis_obj1.z, 0.0f);
        joint.hinge_anchor_obj1 = glm::vec4(hinge_anchor_obj1.x, hinge_anchor_obj1.y, hinge_anchor_obj1.z, 0.0f);
        joint.initial_rel_pos_local =
            glm::vec4(initial_rel_pos_local.x, initial_rel_pos_local.y, initial_rel_pos_local.z, 0.0f);
        joint.initial_rel_rotation =
            glm::vec4(initial_rel_rotation.x, initial_rel_rotation.y, initial_rel_rotation.z, initial_rel_rotation.w);
        m_hinge_joints.push_back(joint);
    }

    uint32_t PhysicsScene::AllocateFixedJoint() {
        GpuFixedJoint joint{};
        joint.obj1_index = INVALID_INDEX;
        joint.obj2_index = INVALID_INDEX;
        joint.compliance = 0.0f;
        m_fixed_joints.push_back(joint);
        return static_cast<uint32_t>(m_fixed_joints.size() - 1);
    }

    void PhysicsScene::UpdateFixedJoint(
        uint32_t joint_idx,
        uint32_t obj1_index,
        uint32_t obj2_index,
        float compliance,
        const glm::vec3 &initial_rel_pos_local,
        const glm::quat &initial_rel_rotation
    ) {
        if (joint_idx >= m_fixed_joints.size()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "UpdateFixedJoint: joint index %u out of range", joint_idx);
            return;
        }
        GpuFixedJoint &joint = m_fixed_joints[joint_idx];
        joint.obj1_index = obj1_index;
        joint.obj2_index = obj2_index;
        joint.compliance = compliance;
        joint.initial_rel_pos_local =
            glm::vec4(initial_rel_pos_local.x, initial_rel_pos_local.y, initial_rel_pos_local.z, 0.0f);
        joint.initial_rel_rotation =
            glm::vec4(initial_rel_rotation.x, initial_rel_rotation.y, initial_rel_rotation.z, initial_rel_rotation.w);
    }

    uint32_t PhysicsScene::AllocateHingeJoint() {
        GpuHingeJoint joint{};
        joint.obj1_index = INVALID_INDEX;
        joint.obj2_index = INVALID_INDEX;
        joint.compliance = 0.0f;
        m_hinge_joints.push_back(joint);
        return static_cast<uint32_t>(m_hinge_joints.size() - 1);
    }

    void PhysicsScene::UpdateHingeJoint(
        uint32_t joint_idx,
        uint32_t obj1_index,
        uint32_t obj2_index,
        float compliance,
        const glm::vec3 &hinge_axis_obj1,
        const glm::vec3 &hinge_anchor_obj1,
        const glm::vec3 &initial_rel_pos_local,
        const glm::quat &initial_rel_rotation
    ) {
        if (joint_idx >= m_hinge_joints.size()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "UpdateHingeJoint: joint index %u out of range", joint_idx);
            return;
        }
        GpuHingeJoint &joint = m_hinge_joints[joint_idx];
        joint.obj1_index = obj1_index;
        joint.obj2_index = obj2_index;
        joint.compliance = compliance;
        joint.hinge_axis_obj1 = glm::vec4(hinge_axis_obj1.x, hinge_axis_obj1.y, hinge_axis_obj1.z, 0.0f);
        joint.hinge_anchor_obj1 = glm::vec4(hinge_anchor_obj1.x, hinge_anchor_obj1.y, hinge_anchor_obj1.z, 0.0f);
        joint.initial_rel_pos_local =
            glm::vec4(initial_rel_pos_local.x, initial_rel_pos_local.y, initial_rel_pos_local.z, 0.0f);
        joint.initial_rel_rotation =
            glm::vec4(initial_rel_rotation.x, initial_rel_rotation.y, initial_rel_rotation.z, initial_rel_rotation.w);
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
        const glm::vec3 &feature,
        const glm::vec3 &shape_world_position,
        const glm::quat &shape_world_rotation,
        const std::vector<ObjectHandle> &ignore_collision_objects
    ) {
        if (!IsShapeIndexValid(shape_index)) {
            return;
        }

        m_shape_type[shape_index] = static_cast<uint32_t>(shape_type);
        m_shape_feature[shape_index] = ToVec4(feature);
        m_shape_world_position[shape_index] = ToVec4(shape_world_position);
        m_shape_world_rotation[shape_index] = ToVec4(glm::normalize(shape_world_rotation));
        const uint32_t rigid_body_index = m_shape_to_rigid_body[shape_index];
        if (rigid_body_index == INVALID_INDEX) {
            m_shape_position[shape_index] = ToVec4(shape_world_position);
            m_shape_rotation[shape_index] = ToVec4(glm::normalize(shape_world_rotation));
        } else {
            EnqueueRigidBodyInitialization(rigid_body_index);
        }

        // Update filter handles if the list changed.
        if (shape_index < m_pending_filter_handles.size()
            && m_pending_filter_handles[shape_index] != ignore_collision_objects) {
            m_pending_filter_handles[shape_index] = ignore_collision_objects;
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
        m_rigid_body_is_kinematic[rigid_body_index] = is_kinematic ? 1u : 0u;
        m_rigid_body_linear_velocity[rigid_body_index] = ToVec4(linear_velocity);
        m_rigid_body_angular_velocity[rigid_body_index] = ToVec4(angular_velocity_axis_angle);
        m_rigid_body_external_force[rigid_body_index] = ToVec4(external_force);
        m_rigid_body_external_torque[rigid_body_index] = ToVec4(external_torque);

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

    void PhysicsScene::InitializePendingRigidBodies(RenderSystem &render_system) {
        if (m_rigid_body_init_queue.empty()) {
            return;
        }

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

        RefreshGpuBuffers(render_system);
        render_system.GetFrameManager().GetSubmissionHelper().ExecuteSubmissionImmediately();

        // Notify SceneDataManager about the model matrices buffer so it can
        // be included in the scene descriptor set (set 0, binding 2).
        render_system.GetSceneDataManager().SetModelMatricesBuffer(m_gpu_model_matrices.get());
    }

    PhysicsScene::PhysicsGpuBuffers PhysicsScene::GetGpuBuffers() const noexcept {
        return {
            m_gpu_rigid_body_alive.get(),
            m_gpu_rigid_body_mass.get(),
            m_gpu_rigid_body_static_friction.get(),
            m_gpu_rigid_body_dynamic_friction.get(),
            m_gpu_rigid_body_restitution.get(),
            m_gpu_rigid_body_is_kinematic.get(),
            m_gpu_rigid_body_center_world_position.get(),
            m_gpu_rigid_body_center_world_rotation.get(),
            m_gpu_rigid_body_center_offset_local_position.get(),
            m_gpu_rigid_body_inertia.get(),
            m_gpu_rigid_body_inverse_inertia.get(),
            m_gpu_rigid_body_linear_velocity.get(),
            m_gpu_rigid_body_angular_velocity.get(),
            m_gpu_rigid_body_external_force.get(),
            m_gpu_rigid_body_external_torque.get(),
            m_gpu_shape_alive.get(),
            m_gpu_shape_type.get(),
            m_gpu_shape_bound_rigid_body.get(),
            m_gpu_shape_feature.get(),
            m_gpu_shape_local_position.get(),
            m_gpu_shape_local_rotation.get(),
            m_gpu_shape_world_position.get(),
            m_gpu_shape_world_rotation.get(),
            m_gpu_rigid_body_shape_offset.get(),
            m_gpu_rigid_body_shape_count.get(),
            m_gpu_flattened_shape_indices.get(),
            m_gpu_model_matrices.get(),
            m_gpu_shape_filter_offset.get(),
            m_gpu_shape_filter_count.get(),
            m_gpu_shape_filter_data.get(),
            m_gpu_fixed_joints.get(),
            m_gpu_hinge_joints.get(),
            m_gpu_rigid_body_slot_count,
            m_gpu_shape_slot_count,
            static_cast<uint32_t>(m_fixed_joints.size()),
            static_cast<uint32_t>(m_hinge_joints.size()),
        };
    }

    bool PhysicsScene::IsRigidBodyIndexValid(uint32_t rigid_body_index) const {
        return rigid_body_index < m_rigid_body_alive.size() && m_rigid_body_alive[rigid_body_index] != 0u;
    }

    bool PhysicsScene::IsShapeIndexValid(uint32_t shape_index) const {
        return shape_index < m_shape_alive.size() && m_shape_alive[shape_index] != 0u;
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

        glm::vec3 object_world_position = Vec4ToVec3(m_rigid_body_center_world_position[rigid_body_index])
                                          - Vec4ToVec3(m_rigid_body_center_offset_local_position[rigid_body_index]);
        glm::quat object_world_rotation = Vec4ToQuat(m_rigid_body_center_world_rotation[rigid_body_index]);

        auto map_iter = m_rigid_body_to_shapes.find(rigid_body_index);
        if (map_iter == m_rigid_body_to_shapes.end() || map_iter->second.empty()) {
            m_rigid_body_center_world_position[rigid_body_index] = ToVec4(object_world_position);
            m_rigid_body_center_world_rotation[rigid_body_index] = ToVec4(object_world_rotation);
            m_rigid_body_center_offset_local_position[rigid_body_index] = glm::vec4(0.0f);
            m_rigid_body_inertia[rigid_body_index] = glm::mat4(0.0f);
            m_rigid_body_inverse_inertia[rigid_body_index] = glm::mat4(0.0f);
            return;
        }

        // If manual inertia is set, use it directly and skip automatic computation.
        if (rigid_body_index < m_rigid_body_use_manual_inertia.size()
            && m_rigid_body_use_manual_inertia[rigid_body_index]) {
            m_rigid_body_inertia[rigid_body_index] = glm::mat4(m_rigid_body_manual_inertia[rigid_body_index]);
            const float det = glm::determinant(m_rigid_body_manual_inertia[rigid_body_index]);
            if (det > 1e-12f) {
                m_rigid_body_inverse_inertia[rigid_body_index] =
                    glm::mat4(glm::inverse(m_rigid_body_manual_inertia[rigid_body_index]));
            } else {
                m_rigid_body_inverse_inertia[rigid_body_index] = glm::mat4(0.0f);
            }
            // Center-of-mass stays at the GameObject's world position/orientation.
            m_rigid_body_center_world_position[rigid_body_index] = ToVec4(object_world_position);
            m_rigid_body_center_world_rotation[rigid_body_index] = ToVec4(object_world_rotation);
            m_rigid_body_center_offset_local_position[rigid_body_index] = glm::vec4(0.0f);
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

            const glm::vec3 feature = glm::abs(Vec4ToVec3(m_shape_feature[shape_index]));
            const uint32_t st = m_shape_type[shape_index];
            float volume = 0.0f;

            switch (st) {
            case 0u: // Box
                volume = ComputeBoxVolume(feature);
                break;
            case 1u: // Sphere
                volume = ComputeSphereVolume(feature.x);
                break;
            case 2u: // Cylinder
                volume = ComputeCylinderVolume(feature.x, feature.y);
                break;
            default:
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "RecalculateRigidBodyState: unknown shape type %u at index %u — skipping",
                    st,
                    shape_index
                );
                break;
            }

            if (volume > 0.0f) {
                total_volume += volume;
                weighted_center_world += Vec4ToVec3(m_shape_world_position[shape_index]) * volume;
            }
        }

        if (valid_shape_indices.empty()) {
            m_rigid_body_center_world_position[rigid_body_index] = ToVec4(object_world_position);
            m_rigid_body_center_world_rotation[rigid_body_index] = ToVec4(object_world_rotation);
            m_rigid_body_center_offset_local_position[rigid_body_index] = glm::vec4(0.0f);
            m_rigid_body_inertia[rigid_body_index] = glm::mat4(0.0f);
            m_rigid_body_inverse_inertia[rigid_body_index] = glm::mat4(0.0f);
            return;
        }

        glm::vec3 center_world_position = weighted_center_world / total_volume;
        const glm::quat center_world_rotation = object_world_rotation;
        const glm::quat inv_center_world_rotation = glm::inverse(center_world_rotation);
        const glm::vec3 center_offset_local_position =
            inv_center_world_rotation * (center_world_position - object_world_position);

        m_rigid_body_center_world_position[rigid_body_index] = ToVec4(center_world_position);
        m_rigid_body_center_world_rotation[rigid_body_index] = ToVec4(center_world_rotation);
        m_rigid_body_center_offset_local_position[rigid_body_index] = ToVec4(center_offset_local_position);

        glm::mat3 inertia_tensor(0.0f);
        const float total_mass = std::max(m_rigid_body_mass[rigid_body_index], 0.0f);
        const float fallback_mass =
            valid_shape_indices.empty() ? 0.0f : (total_mass / static_cast<float>(valid_shape_indices.size()));

        for (uint32_t shape_index : valid_shape_indices) {
            const glm::vec3 feature = glm::abs(Vec4ToVec3(m_shape_feature[shape_index]));
            const uint32_t st = m_shape_type[shape_index];

            float volume = 0.0f;
            switch (st) {
            case 0u:
                volume = ComputeBoxVolume(feature);
                break;
            case 1u:
                volume = ComputeSphereVolume(feature.x);
                break;
            case 2u:
                volume = ComputeCylinderVolume(feature.x, feature.y);
                break;
            default:
                break;
            }

            if (volume <= 0.0f) {
                continue;
            }

            const float mass = total_volume > 1e-6f ? (total_mass * (volume / total_volume)) : fallback_mass;

            const glm::vec3 shape_world_position = Vec4ToVec3(m_shape_world_position[shape_index]);
            const glm::quat shape_world_rotation = glm::normalize(Vec4ToQuat(m_shape_world_rotation[shape_index]));

            const glm::vec3 shape_local_position =
                inv_center_world_rotation * (shape_world_position - center_world_position);
            const glm::quat shape_local_rotation = glm::normalize(inv_center_world_rotation * shape_world_rotation);

            m_shape_position[shape_index] = ToVec4(shape_local_position);
            m_shape_rotation[shape_index] = ToVec4(shape_local_rotation);

            glm::mat3 inertia_shape(0.0f);
            switch (st) {
            case 0u: // Box
                inertia_shape = ComputeBoxInertia(mass, feature);
                break;
            case 1u: // Sphere
                inertia_shape = ComputeSphereInertia(mass, feature.x);
                break;
            case 2u: // Cylinder
                inertia_shape = ComputeCylinderInertia(mass, feature.x, feature.y);
                break;
            default: // Unknown type — already warned in first pass
                continue;
            }

            const glm::mat3 rotation_matrix = glm::mat3_cast(shape_local_rotation);
            const glm::mat3 rotated_inertia = rotation_matrix * inertia_shape * glm::transpose(rotation_matrix);

            const glm::vec3 d = shape_local_position;
            const float d2 = glm::dot(d, d);
            const glm::mat3 identity(1.0f);
            const glm::mat3 parallel_axis = mass * ((d2 * identity) - glm::outerProduct(d, d));

            inertia_tensor += rotated_inertia + parallel_axis;
        }

        m_rigid_body_inertia[rigid_body_index] = glm::mat4(inertia_tensor);
        const float det = glm::determinant(inertia_tensor);
        if (glm::abs(det) > 1e-12f) {
            m_rigid_body_inverse_inertia[rigid_body_index] = glm::mat4(glm::inverse(inertia_tensor));
        } else {
            m_rigid_body_inverse_inertia[rigid_body_index] = glm::mat4(0.0f);
        }
    }

    void PhysicsScene::RefreshGpuBuffers(RenderSystem &render_system) {
        m_gpu_rigid_body_slot_count = static_cast<uint32_t>(m_rigid_body_alive.size());
        m_gpu_shape_slot_count = static_cast<uint32_t>(m_shape_alive.size());

        std::vector<uint32_t> rigid_body_shape_offset(m_gpu_rigid_body_slot_count, 0u);
        std::vector<uint32_t> rigid_body_shape_count(m_gpu_rigid_body_slot_count, 0u);
        std::vector<uint32_t> flattened_shape_indices{};
        flattened_shape_indices.reserve(m_gpu_shape_slot_count);

        for (uint32_t rigid_body_index = 0; rigid_body_index < m_gpu_rigid_body_slot_count; rigid_body_index++) {
            rigid_body_shape_offset[rigid_body_index] = static_cast<uint32_t>(flattened_shape_indices.size());
            if (m_rigid_body_alive[rigid_body_index] == 0u) {
                continue;
            }

            auto iter = m_rigid_body_to_shapes.find(rigid_body_index);
            if (iter == m_rigid_body_to_shapes.end()) {
                continue;
            }

            for (uint32_t shape_index : iter->second) {
                if (!IsShapeIndexValid(shape_index)) {
                    continue;
                }
                if (m_shape_to_rigid_body[shape_index] != rigid_body_index) {
                    continue;
                }

                flattened_shape_indices.push_back(shape_index);
                rigid_body_shape_count[rigid_body_index] += 1u;
            }
        }
        m_gpu_flattened_shape_index_count = static_cast<uint32_t>(flattened_shape_indices.size());

        const auto &allocator = render_system.GetAllocatorState();
        EnsureBuffer<uint32_t>(m_gpu_rigid_body_alive, allocator, m_gpu_rigid_body_slot_count, "Physics RB Alive");
        EnsureBuffer<float>(m_gpu_rigid_body_mass, allocator, m_gpu_rigid_body_slot_count, "Physics RB Mass");
        EnsureBuffer<float>(
            m_gpu_rigid_body_static_friction, allocator, m_gpu_rigid_body_slot_count, "Physics RB StaticFriction"
        );
        EnsureBuffer<float>(
            m_gpu_rigid_body_dynamic_friction, allocator, m_gpu_rigid_body_slot_count, "Physics RB DynamicFriction"
        );
        EnsureBuffer<float>(
            m_gpu_rigid_body_restitution, allocator, m_gpu_rigid_body_slot_count, "Physics RB Restitution"
        );
        EnsureBuffer<uint32_t>(
            m_gpu_rigid_body_is_kinematic, allocator, m_gpu_rigid_body_slot_count, "Physics RB IsKinematic"
        );
        EnsureBuffer<glm::vec4>(
            m_gpu_rigid_body_center_world_position, allocator, m_gpu_rigid_body_slot_count, "Physics RB CenterPos"
        );
        EnsureBuffer<glm::vec4>(
            m_gpu_rigid_body_center_world_rotation, allocator, m_gpu_rigid_body_slot_count, "Physics RB CenterRot"
        );
        EnsureBuffer<glm::vec4>(
            m_gpu_rigid_body_center_offset_local_position,
            allocator,
            m_gpu_rigid_body_slot_count,
            "Physics RB CenterOff"
        );
        EnsureBuffer<glm::mat4>(m_gpu_rigid_body_inertia, allocator, m_gpu_rigid_body_slot_count, "Physics RB Inertia");
        EnsureBuffer<glm::mat4>(
            m_gpu_rigid_body_inverse_inertia, allocator, m_gpu_rigid_body_slot_count, "Physics RB InvInertia"
        );
        EnsureBuffer<glm::vec4>(
            m_gpu_rigid_body_linear_velocity, allocator, m_gpu_rigid_body_slot_count, "Physics RB LinVel"
        );
        EnsureBuffer<glm::vec4>(
            m_gpu_rigid_body_angular_velocity, allocator, m_gpu_rigid_body_slot_count, "Physics RB AngVel"
        );
        EnsureBuffer<glm::vec4>(
            m_gpu_rigid_body_external_force, allocator, m_gpu_rigid_body_slot_count, "Physics RB ExtForce"
        );
        EnsureBuffer<glm::vec4>(
            m_gpu_rigid_body_external_torque, allocator, m_gpu_rigid_body_slot_count, "Physics RB ExtTorque"
        );

        EnsureBuffer<uint32_t>(m_gpu_shape_alive, allocator, m_gpu_shape_slot_count, "Physics Shape Alive");
        EnsureBuffer<uint32_t>(m_gpu_shape_type, allocator, m_gpu_shape_slot_count, "Physics Shape Type");
        EnsureBuffer<uint32_t>(
            m_gpu_shape_bound_rigid_body, allocator, m_gpu_shape_slot_count, "Physics Shape BoundRB"
        );
        EnsureBuffer<glm::vec4>(m_gpu_shape_feature, allocator, m_gpu_shape_slot_count, "Physics Shape Feature");
        EnsureBuffer<glm::vec4>(
            m_gpu_shape_local_position, allocator, m_gpu_shape_slot_count, "Physics Shape LocalPos"
        );
        EnsureBuffer<glm::vec4>(
            m_gpu_shape_local_rotation, allocator, m_gpu_shape_slot_count, "Physics Shape LocalRot"
        );
        EnsureBuffer<glm::vec4>(
            m_gpu_shape_world_position, allocator, m_gpu_shape_slot_count, "Physics Shape WorldPos"
        );
        EnsureBuffer<glm::vec4>(
            m_gpu_shape_world_rotation, allocator, m_gpu_shape_slot_count, "Physics Shape WorldRot"
        );

        EnsureBuffer<uint32_t>(
            m_gpu_rigid_body_shape_offset, allocator, m_gpu_rigid_body_slot_count, "Physics RB ShapeOff"
        );
        EnsureBuffer<uint32_t>(
            m_gpu_rigid_body_shape_count, allocator, m_gpu_rigid_body_slot_count, "Physics RB ShapeCnt"
        );
        EnsureBuffer<uint32_t>(
            m_gpu_flattened_shape_indices, allocator, m_gpu_flattened_shape_index_count, "Physics FlatShapes"
        );

        EnsureBuffer<glm::mat4>(m_gpu_model_matrices, allocator, m_gpu_rigid_body_slot_count, "Physics ModelMatrices");

        // Collision filter buffers.
        EnsureBuffer<uint32_t>(m_gpu_shape_filter_offset, allocator, m_gpu_shape_slot_count, "Physics ShapeFilterOff");
        EnsureBuffer<uint32_t>(m_gpu_shape_filter_count, allocator, m_gpu_shape_slot_count, "Physics ShapeFilterCnt");
        EnsureBuffer<uint32_t>(
            m_gpu_shape_filter_data, allocator, m_shape_filter_data.size(), "Physics ShapeFilterData"
        );

        auto &submission = render_system.GetFrameManager().GetSubmissionHelper();
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_alive, MakeSpan(m_rigid_body_alive));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_mass, MakeSpan(m_rigid_body_mass));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_static_friction, MakeSpan(m_rigid_body_static_friction));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_dynamic_friction, MakeSpan(m_rigid_body_dynamic_friction));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_restitution, MakeSpan(m_rigid_body_restitution));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_is_kinematic, MakeSpan(m_rigid_body_is_kinematic));
        submission.EnqueueBufferSubmission(
            *m_gpu_rigid_body_center_world_position, MakeSpan(m_rigid_body_center_world_position)
        );
        submission.EnqueueBufferSubmission(
            *m_gpu_rigid_body_center_world_rotation, MakeSpan(m_rigid_body_center_world_rotation)
        );
        submission.EnqueueBufferSubmission(
            *m_gpu_rigid_body_center_offset_local_position, MakeSpan(m_rigid_body_center_offset_local_position)
        );
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_inertia, MakeSpan(m_rigid_body_inertia));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_inverse_inertia, MakeSpan(m_rigid_body_inverse_inertia));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_linear_velocity, MakeSpan(m_rigid_body_linear_velocity));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_angular_velocity, MakeSpan(m_rigid_body_angular_velocity));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_external_force, MakeSpan(m_rigid_body_external_force));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_external_torque, MakeSpan(m_rigid_body_external_torque));

        submission.EnqueueBufferSubmission(*m_gpu_shape_alive, MakeSpan(m_shape_alive));
        submission.EnqueueBufferSubmission(*m_gpu_shape_type, MakeSpan(m_shape_type));
        submission.EnqueueBufferSubmission(*m_gpu_shape_bound_rigid_body, MakeSpan(m_shape_to_rigid_body));
        submission.EnqueueBufferSubmission(*m_gpu_shape_feature, MakeSpan(m_shape_feature));
        submission.EnqueueBufferSubmission(*m_gpu_shape_local_position, MakeSpan(m_shape_position));
        submission.EnqueueBufferSubmission(*m_gpu_shape_local_rotation, MakeSpan(m_shape_rotation));
        submission.EnqueueBufferSubmission(*m_gpu_shape_world_position, MakeSpan(m_shape_world_position));
        submission.EnqueueBufferSubmission(*m_gpu_shape_world_rotation, MakeSpan(m_shape_world_rotation));

        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_shape_offset, MakeSpan(rigid_body_shape_offset));
        submission.EnqueueBufferSubmission(*m_gpu_rigid_body_shape_count, MakeSpan(rigid_body_shape_count));
        submission.EnqueueBufferSubmission(*m_gpu_flattened_shape_indices, MakeSpan(flattened_shape_indices));

        // Collision filter data.
        submission.EnqueueBufferSubmission(*m_gpu_shape_filter_offset, MakeSpan(m_shape_filter_offset));
        submission.EnqueueBufferSubmission(*m_gpu_shape_filter_count, MakeSpan(m_shape_filter_count));
        submission.EnqueueBufferSubmission(*m_gpu_shape_filter_data, MakeSpan(m_shape_filter_data));

        // Joint constraint buffers.
        const uint32_t fixed_joint_count = static_cast<uint32_t>(m_fixed_joints.size());
        const uint32_t hinge_joint_count = static_cast<uint32_t>(m_hinge_joints.size());
        EnsureBuffer<GpuFixedJoint>(m_gpu_fixed_joints, allocator, fixed_joint_count, "Physics FixedJoints");
        EnsureBuffer<GpuHingeJoint>(m_gpu_hinge_joints, allocator, hinge_joint_count, "Physics HingeJoints");
        if (fixed_joint_count > 0) {
            submission.EnqueueBufferSubmission(*m_gpu_fixed_joints, MakeSpan(m_fixed_joints));
        }
        if (hinge_joint_count > 0) {
            submission.EnqueueBufferSubmission(*m_gpu_hinge_joints, MakeSpan(m_hinge_joints));
        }
    }

    void PhysicsScene::ResolveCollisionFilters(Scene &scene) {
        const size_t shape_count = m_shape_alive.size();

        // Phase 1: Resolve ObjectHandles to shape indices for each shape.
        std::vector<std::vector<uint32_t>> resolved_filters(shape_count);

        for (size_t i = 0; i < shape_count && i < m_pending_filter_handles.size(); ++i) {
            if (m_pending_filter_handles[i].empty()) {
                continue;
            }

            std::vector<uint32_t> resolved;
            for (ObjectHandle obj_handle : m_pending_filter_handles[i]) {
                GameObject *go = scene.GetGameObject(obj_handle);
                if (!go) {
                    continue;
                }

                for (ComponentHandle comp_handle : go->m_components) {
                    auto *shape_comp = scene.GetComponent<CollisionShapeComponent>(comp_handle);
                    if (!shape_comp) {
                        continue;
                    }

                    const uint32_t target_shape_index = shape_comp->GetPhysicsShapeIndex();
                    if (target_shape_index != INVALID_INDEX && IsShapeIndexValid(target_shape_index)) {
                        resolved.push_back(target_shape_index);
                    }
                }
            }

            // Sort and deduplicate.
            std::sort(resolved.begin(), resolved.end());
            resolved.erase(std::unique(resolved.begin(), resolved.end()), resolved.end());

            resolved_filters[i] = std::move(resolved);
        }

        // Phase 2: Enforce symmetry — if A's filter contains B, B's filter must contain A.
        for (size_t i = 0; i < shape_count; ++i) {
            for (uint32_t j : resolved_filters[i]) {
                if (j < shape_count) {
                    auto &other_list = resolved_filters[j];
                    if (std::find(other_list.begin(), other_list.end(), static_cast<uint32_t>(i)) == other_list.end()) {
                        other_list.push_back(static_cast<uint32_t>(i));
                        std::sort(other_list.begin(), other_list.end());
                    }
                }
            }
        }

        // Phase 3: Build flat arrays m_shape_filter_offset, m_shape_filter_count, m_shape_filter_data.
        m_shape_filter_offset.resize(shape_count);
        m_shape_filter_count.resize(shape_count);
        m_shape_filter_data.clear();

        size_t total_entries = 0;
        for (const auto &list : resolved_filters) {
            total_entries += list.size();
        }
        m_shape_filter_data.reserve(total_entries);

        for (size_t i = 0; i < shape_count; ++i) {
            m_shape_filter_offset[i] = static_cast<uint32_t>(m_shape_filter_data.size());
            m_shape_filter_count[i] = static_cast<uint32_t>(resolved_filters[i].size());
            m_shape_filter_data.insert(
                m_shape_filter_data.end(), resolved_filters[i].begin(), resolved_filters[i].end()
            );
        }

        // Clear pending handles.
        m_pending_filter_handles.clear();
        m_pending_filter_handles.resize(shape_count);
    }

    void PhysicsScene::SetRigidBodyTransform(
        uint32_t rigid_body_index, const glm::vec3 &world_position, const glm::quat &world_rotation
    ) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION, "SetRigidBodyTransform: rigid body index %u is invalid", rigid_body_index
            );
            return;
        }
        m_rigid_body_center_world_position[rigid_body_index] = glm::vec4(world_position, 0.0f);
        m_rigid_body_center_world_rotation[rigid_body_index] =
            glm::vec4(world_rotation.x, world_rotation.y, world_rotation.z, world_rotation.w);
    }

    void PhysicsScene::SetModelMatrixActive(uint32_t rigid_body_index, bool active) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION, "SetModelMatrixActive: rigid body index %u is invalid", rigid_body_index
            );
            return;
        }
        m_rigid_body_model_matrix_active[rigid_body_index] = active;
    }

    bool PhysicsScene::IsModelMatrixActive(uint32_t rigid_body_index) const noexcept {
        if (rigid_body_index >= m_rigid_body_model_matrix_active.size()) {
            return false;
        }
        return m_rigid_body_model_matrix_active[rigid_body_index];
    }

    void PhysicsScene::SetSimulationEnabled(bool enabled) {
        m_simulation_enabled = enabled;
        if (!enabled) {
            for (size_t i = 0; i < m_rigid_body_model_matrix_active.size(); ++i) {
                m_rigid_body_model_matrix_active[i] = false;
            }
        }
    }

    bool PhysicsScene::IsSimulationEnabled() const noexcept {
        return m_simulation_enabled;
    }

    void PhysicsScene::DebugPrint() const {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "PhysicsScene %u:", m_scene_id);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "  Rigid bodies:");
        for (size_t i = 0; i < m_rigid_body_alive.size(); i++) {
            if (m_rigid_body_alive[i] == 0u) {
                continue;
            }
            SDL_LogInfo(
                SDL_LOG_CATEGORY_APPLICATION,
                "    [%02u] object=%02u props={mass=%.2f, static_friction=%.2f, dynamic_friction=%.2f, "
                "restitution=%.2f, is_kinematic=%s}\n"
                "       center_world_pos=(%.2f, %.2f, %.2f) center_world_rot=(%.2f, %.2f, %.2f, %.2f)\n"
                "       center_offset_local_pos=(%.2f, %.2f, %.2f)\n"
                "       inertia_local=[[%.2f, %.2f, %.2f], [%.2f, %.2f, %.2f], [%.2f, %.2f, %.2f]] need_init=%s\n"
                "       linear_velocity=(%.2f, %.2f, %.2f) angular_velocity=(%.2f, %.2f, %.2f)\n"
                "       external_force=(%.2f, %.2f, %.2f) external_torque=(%.2f, %.2f, %.2f)",
                static_cast<unsigned int>(i),
                m_rigid_body_to_object[i].GetID(),
                m_rigid_body_mass[i],
                m_rigid_body_static_friction[i],
                m_rigid_body_dynamic_friction[i],
                m_rigid_body_restitution[i],
                m_rigid_body_is_kinematic[i] != 0u ? "true" : "false",
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
                m_rigid_body_inertia[i][0][0],
                m_rigid_body_inertia[i][0][1],
                m_rigid_body_inertia[i][0][2],
                m_rigid_body_inertia[i][1][0],
                m_rigid_body_inertia[i][1][1],
                m_rigid_body_inertia[i][1][2],
                m_rigid_body_inertia[i][2][0],
                m_rigid_body_inertia[i][2][1],
                m_rigid_body_inertia[i][2][2],
                (i < m_rigid_body_need_init.size() && m_rigid_body_need_init[i]) ? "true" : "false",
                m_rigid_body_linear_velocity[i].x,
                m_rigid_body_linear_velocity[i].y,
                m_rigid_body_linear_velocity[i].z,
                m_rigid_body_angular_velocity[i].x,
                m_rigid_body_angular_velocity[i].y,
                m_rigid_body_angular_velocity[i].z,
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
            if (m_shape_alive[i] == 0u) {
                continue;
            }
            const bool bound = m_shape_to_rigid_body[i] != INVALID_INDEX;
            const char *mode = bound ? "local_to_com" : "world";
            SDL_LogInfo(
                SDL_LOG_CATEGORY_APPLICATION,
                "    [%02u] component=%02u type=%d bound_rigidbody=%d mode=%s\n"
                "           feature=(%.2f, %.2f, %.2f) pose_pos=(%.2f, %.2f, %.2f) "
                "pose_rot=(%.2f, %.2f, %.2f, %.2f)",
                static_cast<unsigned int>(i),
                m_shape_index_to_component[i].GetID(),
                static_cast<int>(m_shape_type[i]),
                m_shape_to_rigid_body[i],
                mode,
                m_shape_feature[i].x,
                m_shape_feature[i].y,
                m_shape_feature[i].z,
                m_shape_position[i].x,
                m_shape_position[i].y,
                m_shape_position[i].z,
                m_shape_rotation[i].x,
                m_shape_rotation[i].y,
                m_shape_rotation[i].z,
                m_shape_rotation[i].w
            );
        }
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "  Joints: fixed=%u hinge=%u",
            static_cast<unsigned int>(m_fixed_joints.size()),
            static_cast<unsigned int>(m_hinge_joints.size())
        );
    }
} // namespace Engine

#include "__generated__/PhysicsScene.h.inc"
