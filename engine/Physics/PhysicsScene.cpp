#include "PhysicsScene.h"

#include <Render/Memory/ComputeBuffer.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/RenderSystem/SceneDataManager.h>
#include <Render/RenderSystem/SubmissionHelper.h>
#include <vulkan/vulkan.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cassert>
#include <span>

#include <Framework/world/physics/PhysicsDescriptors.h>

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

        m_rigid_body_mass.clear();
        m_rigid_body_static_friction.clear();
        m_rigid_body_dynamic_friction.clear();
        m_rigid_body_restitution.clear();
        m_rigid_body_is_kinematic.clear();
        m_rigid_body_center_world_position.clear();
        m_rigid_body_center_world_rotation.clear();
        m_rigid_body_inertia.clear();
        m_rigid_body_inverse_inertia.clear();
        m_rigid_body_linear_velocity.clear();
        m_rigid_body_angular_velocity.clear();
        m_rigid_body_external_force.clear();
        m_rigid_body_external_torque.clear();

        m_shape_to_rigid_body.clear();
        m_shape_type.clear();
        m_shape_feature.clear();
        m_shape_local_position.clear();
        m_shape_local_rotation.clear();
        m_shape_world_position.clear();
        m_shape_world_rotation.clear();

        m_gpu_rigid_body_slot_count = 0;
        m_gpu_shape_slot_count = 0;

        m_fixed_joints.clear();
        m_fixed_joint_alive.clear();
        m_hinge_joints.clear();
        m_hinge_joint_alive.clear();
        m_gpu_fixed_joints.reset();
        m_gpu_fixed_joint_alive.reset();
        m_gpu_hinge_joints.reset();
        m_gpu_hinge_joint_alive.reset();

        m_shape_filter_offset.clear();
        m_shape_filter_count.clear();
        m_shape_filter_data.clear();

        m_gpu_rigid_body_alive.reset();
        m_gpu_rigid_body_mass.reset();
        m_gpu_rigid_body_static_friction.reset();
        m_gpu_rigid_body_dynamic_friction.reset();
        m_gpu_rigid_body_restitution.reset();
        m_gpu_rigid_body_is_kinematic.reset();
        m_gpu_rigid_body_center_world_position.reset();
        m_gpu_rigid_body_center_world_rotation.reset();
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

        m_gpu_model_matrices.reset();

        m_gpu_shape_filter_offset.reset();
        m_gpu_shape_filter_count.reset();
        m_gpu_shape_filter_data.reset();
    }

    uint32_t PhysicsScene::AllocateRigidBodySlot() {
        const uint32_t new_index = static_cast<uint32_t>(m_rigid_body_alive.size());
        m_rigid_body_alive.push_back(1u);
        m_rigid_body_mass.push_back(1.0f);
        m_rigid_body_static_friction.push_back(0.5f);
        m_rigid_body_dynamic_friction.push_back(0.5f);
        m_rigid_body_restitution.push_back(0.0f);
        m_rigid_body_is_kinematic.push_back(0u);
        m_rigid_body_center_world_position.push_back(glm::vec4(0.0f));
        m_rigid_body_center_world_rotation.push_back(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        m_rigid_body_inertia.push_back(glm::mat4(0.0f));
        m_rigid_body_inverse_inertia.push_back(glm::mat4(0.0f));
        m_rigid_body_linear_velocity.push_back(glm::vec4(0.0f));
        m_rigid_body_angular_velocity.push_back(glm::vec4(0.0f));
        m_rigid_body_external_force.push_back(glm::vec4(0.0f));
        m_rigid_body_external_torque.push_back(glm::vec4(0.0f));
        return new_index;
    }

    uint32_t PhysicsScene::AllocateCollisionShapeSlot() {
        const uint32_t new_index = static_cast<uint32_t>(m_shape_alive.size());
        m_shape_alive.push_back(1u);
        m_shape_to_rigid_body.push_back(INVALID_INDEX);
        m_shape_type.push_back(0u);
        m_shape_feature.push_back(glm::vec4(0.5f, 0.5f, 0.5f, 0.0f));
        m_shape_local_position.push_back(glm::vec4(0.0f));
        m_shape_local_rotation.push_back(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        m_shape_world_position.push_back(glm::vec4(0.0f));
        m_shape_world_rotation.push_back(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

        m_shape_filter_offset.push_back(0u);
        m_shape_filter_count.push_back(0u);
        return new_index;
    }

    uint32_t PhysicsScene::AllocateFixedJoint() {
        GpuFixedJoint joint{};
        joint.obj1_index = INVALID_INDEX;
        joint.obj2_index = INVALID_INDEX;
        joint.compliance = 0.0f;
        m_fixed_joints.push_back(joint);
        m_fixed_joint_alive.push_back(1u);
        return static_cast<uint32_t>(m_fixed_joints.size() - 1);
    }

    uint32_t PhysicsScene::AllocateHingeJoint() {
        GpuHingeJoint joint{};
        joint.obj1_index = INVALID_INDEX;
        joint.obj2_index = INVALID_INDEX;
        joint.compliance = 0.0f;
        m_hinge_joints.push_back(joint);
        m_hinge_joint_alive.push_back(1u);
        return static_cast<uint32_t>(m_hinge_joints.size() - 1);
    }

    void PhysicsScene::UnregisterRigidBody(uint32_t rigid_body_index) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) return;
        m_rigid_body_alive[rigid_body_index] = 0u;
    }

    void PhysicsScene::UnregisterCollisionShape(uint32_t shape_index) {
        if (!IsShapeIndexValid(shape_index)) return;
        m_shape_alive[shape_index] = 0u;
    }

    void PhysicsScene::UnregisterFixedJoint(uint32_t joint_index) {
        if (joint_index >= m_fixed_joint_alive.size()) return;
        m_fixed_joint_alive[joint_index] = 0u;
    }

    void PhysicsScene::UnregisterHingeJoint(uint32_t joint_index) {
        if (joint_index >= m_hinge_joint_alive.size()) return;
        m_hinge_joint_alive[joint_index] = 0u;
    }

    void PhysicsScene::SubmitRigidBody(uint32_t rigid_body_index, const RigidBodyComDescriptor &desc) {
        if (!IsRigidBodyIndexValid(rigid_body_index)) return;
        m_rigid_body_mass[rigid_body_index] = desc.mass;
        m_rigid_body_static_friction[rigid_body_index] = desc.static_friction;
        m_rigid_body_dynamic_friction[rigid_body_index] = desc.dynamic_friction;
        m_rigid_body_restitution[rigid_body_index] = desc.restitution;
        m_rigid_body_is_kinematic[rigid_body_index] = desc.is_kinematic ? 1u : 0u;
        m_rigid_body_center_world_position[rigid_body_index] = desc.center_world_position;
        m_rigid_body_center_world_rotation[rigid_body_index] = desc.center_world_rotation;
        m_rigid_body_inertia[rigid_body_index] = desc.inertia;
        m_rigid_body_inverse_inertia[rigid_body_index] = desc.inverse_inertia;
        m_rigid_body_linear_velocity[rigid_body_index] = desc.linear_velocity;
        m_rigid_body_angular_velocity[rigid_body_index] = desc.angular_velocity;
        m_rigid_body_external_force[rigid_body_index] = desc.external_force;
        m_rigid_body_external_torque[rigid_body_index] = desc.external_torque;
    }

    void PhysicsScene::SubmitCollisionShape(uint32_t shape_index, const CollisionShapeComDescriptor &desc) {
        if (!IsShapeIndexValid(shape_index)) return;
        m_shape_type[shape_index] = desc.type;
        m_shape_feature[shape_index] = desc.feature;
        m_shape_local_position[shape_index] = desc.local_position;
        m_shape_local_rotation[shape_index] = desc.local_rotation;
        m_shape_world_position[shape_index] = desc.world_position;
        m_shape_world_rotation[shape_index] = desc.world_rotation;
        m_shape_to_rigid_body[shape_index] = desc.bound_rigid_body;

        m_shape_filter_offset[shape_index] = static_cast<uint32_t>(m_shape_filter_data.size());
        m_shape_filter_count[shape_index] = static_cast<uint32_t>(desc.ignore_shape_indices.size());
        m_shape_filter_data.insert(
            m_shape_filter_data.end(), desc.ignore_shape_indices.begin(), desc.ignore_shape_indices.end()
        );
    }

    void PhysicsScene::SubmitFixedJoint(uint32_t joint_idx, const GpuFixedJoint &joint) {
        if (joint_idx >= m_fixed_joints.size()) return;
        m_fixed_joints[joint_idx] = joint;
        m_fixed_joint_alive[joint_idx] = 1u;
    }

    void PhysicsScene::SubmitHingeJoint(uint32_t joint_idx, const GpuHingeJoint &joint) {
        if (joint_idx >= m_hinge_joints.size()) return;
        m_hinge_joints[joint_idx] = joint;
        m_hinge_joint_alive[joint_idx] = 1u;
    }

    void PhysicsScene::SyncGpuBuffers(RenderSystem &render_system) {
        RefreshGpuBuffers(render_system);
        render_system.GetFrameManager().GetSubmissionHelper().ExecuteSubmissionImmediately();
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
            m_gpu_model_matrices.get(),
            m_gpu_shape_filter_offset.get(),
            m_gpu_shape_filter_count.get(),
            m_gpu_shape_filter_data.get(),
            m_gpu_fixed_joints.get(),
            m_gpu_fixed_joint_alive.get(),
            m_gpu_hinge_joints.get(),
            m_gpu_hinge_joint_alive.get(),
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

    void PhysicsScene::SetSimulationEnabled(bool enabled) {
        m_simulation_enabled = enabled;
    }

    bool PhysicsScene::IsSimulationEnabled() const noexcept {
        return m_simulation_enabled;
    }

    void PhysicsScene::RefreshGpuBuffers(RenderSystem &render_system) {
        m_gpu_rigid_body_slot_count = static_cast<uint32_t>(m_rigid_body_alive.size());
        m_gpu_shape_slot_count = static_cast<uint32_t>(m_shape_alive.size());

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

        EnsureBuffer<glm::mat4>(m_gpu_model_matrices, allocator, m_gpu_rigid_body_slot_count, "Physics ModelMatrices");

        EnsureBuffer<uint32_t>(m_gpu_shape_filter_offset, allocator, m_gpu_shape_slot_count, "Physics ShapeFilterOff");
        EnsureBuffer<uint32_t>(m_gpu_shape_filter_count, allocator, m_gpu_shape_slot_count, "Physics ShapeFilterCnt");
        EnsureBuffer<uint32_t>(
            m_gpu_shape_filter_data, allocator, m_shape_filter_data.size(), "Physics ShapeFilterData"
        );

        const uint32_t fixed_joint_count = static_cast<uint32_t>(m_fixed_joints.size());
        const uint32_t hinge_joint_count = static_cast<uint32_t>(m_hinge_joints.size());
        EnsureBuffer<GpuFixedJoint>(m_gpu_fixed_joints, allocator, fixed_joint_count, "Physics FixedJoints");
        EnsureBuffer<uint32_t>(m_gpu_fixed_joint_alive, allocator, fixed_joint_count, "Physics FixedJoint Alive");
        EnsureBuffer<GpuHingeJoint>(m_gpu_hinge_joints, allocator, hinge_joint_count, "Physics HingeJoints");
        EnsureBuffer<uint32_t>(m_gpu_hinge_joint_alive, allocator, hinge_joint_count, "Physics HingeJoint Alive");

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
        submission.EnqueueBufferSubmission(*m_gpu_shape_local_position, MakeSpan(m_shape_local_position));
        submission.EnqueueBufferSubmission(*m_gpu_shape_local_rotation, MakeSpan(m_shape_local_rotation));
        submission.EnqueueBufferSubmission(*m_gpu_shape_world_position, MakeSpan(m_shape_world_position));
        submission.EnqueueBufferSubmission(*m_gpu_shape_world_rotation, MakeSpan(m_shape_world_rotation));

        submission.EnqueueBufferSubmission(*m_gpu_shape_filter_offset, MakeSpan(m_shape_filter_offset));
        submission.EnqueueBufferSubmission(*m_gpu_shape_filter_count, MakeSpan(m_shape_filter_count));
        submission.EnqueueBufferSubmission(*m_gpu_shape_filter_data, MakeSpan(m_shape_filter_data));

        if (fixed_joint_count > 0) {
            submission.EnqueueBufferSubmission(*m_gpu_fixed_joints, MakeSpan(m_fixed_joints));
            submission.EnqueueBufferSubmission(*m_gpu_fixed_joint_alive, MakeSpan(m_fixed_joint_alive));
        }
        if (hinge_joint_count > 0) {
            submission.EnqueueBufferSubmission(*m_gpu_hinge_joints, MakeSpan(m_hinge_joints));
            submission.EnqueueBufferSubmission(*m_gpu_hinge_joint_alive, MakeSpan(m_hinge_joint_alive));
        }
    }

    void PhysicsScene::DebugPrint() const {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "PhysicsScene %u:", m_scene_id);
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "  Rigid bodies: %u slots",
            static_cast<unsigned int>(m_rigid_body_alive.size())
        );
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "  Collision shapes: %u slots",
            static_cast<unsigned int>(m_shape_alive.size())
        );
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "  Joints: fixed=%u hinge=%u",
            static_cast<unsigned int>(m_fixed_joints.size()),
            static_cast<unsigned int>(m_hinge_joints.size())
        );
    }
} // namespace Engine

#include "__generated__/PhysicsScene.h.inc"
