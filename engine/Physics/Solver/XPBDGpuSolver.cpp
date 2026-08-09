#include "XPBDGpuSolver.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/Collision/ConvexCollisionDetector.h>
#include <Physics/Collision/SpatialHashBroadDetector.h>
#include <Physics/PhysicsScene.h>
#include <Rhi/ComputeHelpers.h>
#include <Rhi/DeviceContext.h>

#include <Rhi/ComputeBuffer.h>
#include <Rhi/ComputeResourceBinding.h>
#include <Rhi/ComputeStage.h>
#include <Rhi/DeviceBuffer.h>
#include <Rhi/ShaderResourceBinding.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
    std::vector<uint32_t> LoadSpirv(const char *relative_path) {
        std::filesystem::path full = std::filesystem::path(ENGINE_PHYSICS_SPIRV_DIR) / relative_path;
        std::ifstream file(full, std::ios::binary | std::ios::ate);
        if (!file.is_open()) throw std::runtime_error("Failed to open physics SPIR-V: " + full.string());
        const auto size = static_cast<size_t>(file.tellg());
        if (size == 0u || size % sizeof(uint32_t) != 0u)
            throw std::runtime_error("Invalid physics SPIR-V size: " + full.string());
        std::vector<uint32_t> words(size / sizeof(uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size));
        return words;
    }

    const vk::MemoryBarrier2 kComputeBarrier{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
    };

    // Push-constant layouts, matching the per-shader blocks in
    // engine/Physics/shader/solver/XPBDSolver/. The reflected size is the
    // declared size (std430 member layout, no struct-level 16 padding), so
    // vec4 members must come before scalar members (glm::vec4 aligns to 4).
    struct XpbdPushParams {
        glm::vec4 gravity_dt; // xyz = gravity, w = substep dt
    };
    static_assert(sizeof(XpbdPushParams) == 16, "XpbdPushParams must match shader push block");

    struct HingePushParams {
        glm::vec4 gravity_dt;
        uint32_t hinge_joint_count;
    };
    static_assert(sizeof(HingePushParams) == 20, "HingePushParams must match shader push block");

    struct FixedPushParams {
        glm::vec4 gravity_dt;
        uint32_t fixed_joint_count;
    };
    static_assert(sizeof(FixedPushParams) == 20, "FixedPushParams must match shader push block");
} // namespace

namespace Engine {

    struct XpbdGpuSolver::Impl {
        Rhi::DeviceContext &device_context;

        bool shaders_loaded = false;
        XpbdConfig config{};
        uint32_t max_contact_point = 0u;

        std::unique_ptr<SpatialHashBroadDetector> broad_detector{};
        std::unique_ptr<ConvexCollisionDetector> narrow_detector{};

        // ---- Compute stages ----
        std::unique_ptr<Rhi::ComputeStage> clear_int_stage{};
        std::unique_ptr<Rhi::ComputeStage> snapshot_stage{};
        std::unique_ptr<Rhi::ComputeStage> update_shape_world_pose_stage{};
        std::unique_ptr<Rhi::ComputeStage> integrate_stage{};
        std::unique_ptr<Rhi::ComputeStage> accum_pos_stage{};
        std::unique_ptr<Rhi::ComputeStage> apply_pos_stage{};
        std::unique_ptr<Rhi::ComputeStage> update_vel_stage{};
        std::unique_ptr<Rhi::ComputeStage> accum_vel_stage{};
        std::unique_ptr<Rhi::ComputeStage> apply_vel_stage{};
        std::unique_ptr<Rhi::ComputeStage> model_matrix_stage{};
        std::unique_ptr<Rhi::ComputeStage> clear_hinge_lagrange_stage{};
        std::unique_ptr<Rhi::ComputeStage> clear_fixed_lagrange_stage{};
        std::unique_ptr<Rhi::ComputeStage> accum_hinge_pos_stage{};
        std::unique_ptr<Rhi::ComputeStage> accum_fixed_pos_stage{};

        // ---- Pre-allocated bindings ----
        Rhi::ComputeResourceBinding *clear_int_binding = nullptr;
        Rhi::ComputeResourceBinding *snapshot_binding = nullptr;
        Rhi::ComputeResourceBinding *update_shape_world_pose_binding = nullptr;
        Rhi::ComputeResourceBinding *integrate_binding = nullptr;
        Rhi::ComputeResourceBinding *accum_pos_binding = nullptr;
        Rhi::ComputeResourceBinding *apply_pos_binding = nullptr;
        Rhi::ComputeResourceBinding *update_vel_binding = nullptr;
        Rhi::ComputeResourceBinding *accum_vel_binding = nullptr;
        Rhi::ComputeResourceBinding *apply_vel_binding = nullptr;
        Rhi::ComputeResourceBinding *model_matrix_binding = nullptr;
        Rhi::ComputeResourceBinding *clear_hinge_lagrange_binding = nullptr;
        Rhi::ComputeResourceBinding *clear_fixed_lagrange_binding = nullptr;
        Rhi::ComputeResourceBinding *accum_hinge_pos_binding = nullptr;
        Rhi::ComputeResourceBinding *accum_fixed_pos_binding = nullptr;

        // ---- Intermediate GPU buffers ----
        std::unique_ptr<Rhi::ComputeBuffer> gpu_pre_contact_linear_vel{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_pre_contact_angular_vel{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_substep_start_position{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_substep_start_orientation{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_linear_position_delta{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_angular_position_delta{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_position_delta_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_linear_velocity_delta{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_angular_velocity_delta{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_velocity_delta_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_lagrange{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_axis_lagrange{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_anchor_lagrange{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_rotation_lagrange{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_position_lagrange{};

        // CPU-side per-dispatch constants, recorded as push constants in GPUStep.
        glm::vec4 push_gravity_dt{0.0f, 0.0f, -9.81f, 0.0f};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        void EnsureBuffer(std::unique_ptr<Rhi::ComputeBuffer> &buf, size_t bytes, const char *name) {
            const auto &alloc = device_context.GetAllocatorState();
            if (!buf || buf->GetSize() != bytes) {
                buf = Rhi::ComputeBuffer::CreateUnique(alloc, bytes, false, false, false, false, name);
            }
        }

        void EnsureIntermediateBuffers(
            uint32_t body_count, uint32_t max_contacts, uint32_t hinge_joint_count, uint32_t fixed_joint_count
        ) {
            size_t body_bytes = static_cast<size_t>(body_count) * sizeof(glm::vec4);
            size_t body_int3 = static_cast<size_t>(body_count) * 3 * sizeof(float);
            size_t body_int1 = static_cast<size_t>(body_count) * sizeof(float);
            size_t contact_lagrange_bytes = static_cast<size_t>(max_contacts) * sizeof(float);
            size_t hinge_fbytes = static_cast<size_t>(std::max(1u, hinge_joint_count)) * sizeof(float);
            size_t fixed_fbytes = static_cast<size_t>(std::max(1u, fixed_joint_count)) * sizeof(float);

            EnsureBuffer(gpu_pre_contact_linear_vel, body_bytes, "XPBD PreContactLinVel");
            EnsureBuffer(gpu_pre_contact_angular_vel, body_bytes, "XPBD PreContactAngVel");
            EnsureBuffer(gpu_substep_start_position, body_bytes, "XPBD SubstepStartPos");
            EnsureBuffer(gpu_substep_start_orientation, body_bytes, "XPBD SubstepStartOri");
            EnsureBuffer(gpu_linear_position_delta, body_int3, "XPBD LinPosDelta");
            EnsureBuffer(gpu_angular_position_delta, body_int3, "XPBD AngPosDelta");
            EnsureBuffer(gpu_position_delta_count, body_int1, "XPBD PosDeltaCnt");
            EnsureBuffer(gpu_linear_velocity_delta, body_int3, "XPBD LinVelDelta");
            EnsureBuffer(gpu_angular_velocity_delta, body_int3, "XPBD AngVelDelta");
            EnsureBuffer(gpu_velocity_delta_count, body_int1, "XPBD VelDeltaCnt");
            EnsureBuffer(gpu_contact_lagrange, contact_lagrange_bytes, "XPBD ContactLagrange");
            EnsureBuffer(gpu_hinge_axis_lagrange, hinge_fbytes, "XPBD HingeAxisLagrange");
            EnsureBuffer(gpu_hinge_anchor_lagrange, hinge_fbytes, "XPBD HingeAnchorLagrange");
            EnsureBuffer(gpu_fixed_rotation_lagrange, fixed_fbytes, "XPBD FixedRotLagrange");
            EnsureBuffer(gpu_fixed_position_lagrange, fixed_fbytes, "XPBD FixedPosLagrange");
        }

        void EnsureShadersLoaded() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            auto load = [this](const char *path, const char *name) {
                auto spirv = LoadSpirv(path);
                auto stage = std::make_unique<Rhi::ComputeStage>(device_context);
                stage->Instantiate(spirv, name);
                return stage;
            };
            clear_int_stage = load("solver/XPBDSolver/clear_int_buffer.comp.spv", "XPBD Clear Int");
            clear_int_binding = &clear_int_stage->AllocateResourceBinding();

            snapshot_stage = load("solver/XPBDSolver/snapshot_position.comp.spv", "XPBD Snapshot");
            snapshot_binding = &snapshot_stage->AllocateResourceBinding();

            update_shape_world_pose_stage =
                load("solver/XPBDSolver/update_shape_world_pose.comp.spv", "XPBD UpdateShape");
            update_shape_world_pose_binding = &update_shape_world_pose_stage->AllocateResourceBinding();

            integrate_stage = load("solver/XPBDSolver/integrate_forces.comp.spv", "XPBD Integrate");
            integrate_binding = &integrate_stage->AllocateResourceBinding();

            accum_pos_stage = load("solver/XPBDSolver/accumulate_contact_position.comp.spv", "XPBD AccumPos");
            accum_pos_binding = &accum_pos_stage->AllocateResourceBinding();

            apply_pos_stage = load("solver/XPBDSolver/apply_body_position_deltas.comp.spv", "XPBD ApplyPos");
            apply_pos_binding = &apply_pos_stage->AllocateResourceBinding();

            update_vel_stage = load("solver/XPBDSolver/update_velocities_from_pose.comp.spv", "XPBD UpdateVel");
            update_vel_binding = &update_vel_stage->AllocateResourceBinding();

            accum_vel_stage = load("solver/XPBDSolver/accumulate_contact_velocity.comp.spv", "XPBD AccumVel");
            accum_vel_binding = &accum_vel_stage->AllocateResourceBinding();

            apply_vel_stage = load("solver/XPBDSolver/apply_body_velocity_deltas.comp.spv", "XPBD ApplyVel");
            apply_vel_binding = &apply_vel_stage->AllocateResourceBinding();

            model_matrix_stage = load("solver/XPBDSolver/model_matrix.comp.spv", "XPBD ModelMatrix");
            model_matrix_binding = &model_matrix_stage->AllocateResourceBinding();

            clear_hinge_lagrange_stage = load("solver/XPBDSolver/clear_hinge_lagrange.comp.spv", "XPBD ClearHinge");
            clear_hinge_lagrange_binding = &clear_hinge_lagrange_stage->AllocateResourceBinding();

            clear_fixed_lagrange_stage = load("solver/XPBDSolver/clear_fixed_lagrange.comp.spv", "XPBD ClearFixed");
            clear_fixed_lagrange_binding = &clear_fixed_lagrange_stage->AllocateResourceBinding();

            accum_hinge_pos_stage = load("solver/XPBDSolver/accumulate_hinge_position.comp.spv", "XPBD AccumHingePos");
            accum_hinge_pos_binding = &accum_hinge_pos_stage->AllocateResourceBinding();

            accum_fixed_pos_stage = load("solver/XPBDSolver/accumulate_fixed_position.comp.spv", "XPBD AccumFixedPos");
            accum_fixed_pos_binding = &accum_fixed_pos_stage->AllocateResourceBinding();
        }
    };

    XpbdGpuSolver::XpbdGpuSolver(Rhi::DeviceContext &device_context) : m_impl(std::make_unique<Impl>(device_context)) {
    }

    XpbdGpuSolver::~XpbdGpuSolver() = default;

    bool XpbdGpuSolver::IsInitialized() const noexcept {
        return m_impl->shaders_loaded;
    }

    void XpbdGpuSolver::SetConfig(const XpbdConfig &config) noexcept {
        m_impl->config = config;
    }

    const XpbdConfig &XpbdGpuSolver::GetConfig() const noexcept {
        return m_impl->config;
    }

    void XpbdGpuSolver::PreGPUStep() {
        const auto gpu = m_bound_scene->GetGpuBuffers();
        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) return;

        m_impl->EnsureShadersLoaded();

        const uint32_t body_count = gpu.rigid_body_slot_count;
        const uint32_t shape_count = gpu.shape_slot_count;
        const uint32_t all_pairs = shape_count > 1u ? (shape_count * (shape_count - 1u)) / 2u : 0u;
        const uint32_t max_contacts = std::max(1u, std::min(all_pairs * 5u, m_impl->config.max_contact_points));
        m_impl->max_contact_point = max_contacts;

        m_impl->EnsureIntermediateBuffers(body_count, max_contacts, gpu.hinge_joint_count, gpu.fixed_joint_count);

        const float substep_dt =
            m_impl->config.time_step / static_cast<float>(std::max(1u, m_impl->config.num_substep_perstep));
        m_impl->push_gravity_dt =
            glm::vec4(m_impl->config.gravity.x, m_impl->config.gravity.y, m_impl->config.gravity.z, substep_dt);

        {
            if (!m_impl->broad_detector) {
                m_impl->broad_detector = std::make_unique<SpatialHashBroadDetector>(m_impl->device_context);
            }
            GridConfig grid_config{};
            grid_config.world_min = m_impl->config.grid_world_min;
            grid_config.world_max = m_impl->config.grid_world_max;
            grid_config.cell_size = m_impl->config.grid_cell_size;
            grid_config.max_cells_per_shape = m_impl->config.max_cells_per_shape;
            m_impl->broad_detector->Configure(
                *m_bound_scene,
                shape_count,
                grid_config,
                m_impl->config.fallback_all_pairs_threshold,
                m_impl->config.max_global_shape_count
            );
            auto broad_buffers = m_impl->broad_detector->GetResultBuffers();

            if (!m_impl->narrow_detector) {
                m_impl->narrow_detector = std::make_unique<ConvexCollisionDetector>(m_impl->device_context);
            }
            uint32_t broad_max_pairs = m_impl->broad_detector->GetMaxPairs();
            uint32_t narrow_max_contacts =
                std::max(1u, std::min(broad_max_pairs * 5u, m_impl->config.max_contact_points));
            m_impl->narrow_detector->Configure(
                *m_bound_scene,
                broad_buffers.max_pairs,
                narrow_max_contacts,
                m_impl->config.contact_margin,
                *broad_buffers.pair_buffer,
                *broad_buffers.pair_count_buffer
            );
        }
    }

    void XpbdGpuSolver::GPUStep(vk::CommandBuffer cb) {
        const auto gpu = m_bound_scene->GetGpuBuffers();
        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) return;

        const uint32_t body_count = gpu.rigid_body_slot_count;
        const uint32_t shape_count = gpu.shape_slot_count;
        const uint32_t body_wg = (body_count + 63u) / 64u;
        const uint32_t shape_wg = (shape_count + 63u) / 64u;

        auto barrier = [&cb]() { cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}}); };

        auto dispatch = [&cb](
                            Rhi::ComputeStage &stage,
                            Rhi::ComputeResourceBinding &binding,
                            uint32_t x,
                            uint32_t y = 1,
                            uint32_t z = 1
                        ) {
            Rhi::BindComputeStage(cb, stage);
            Rhi::BindComputeResource(cb, stage, binding);
            Rhi::DispatchCompute(cb, x, y, z);
        };

        auto dispatch_clear = [this, &cb, &dispatch](Rhi::ComputeBuffer &tgt, uint32_t elem_count, uint32_t wg) {
            Rhi::PushConstants(cb, *m_impl->clear_int_stage, elem_count);
            auto &srb = m_impl->clear_int_binding->GetShaderResourceBinding();
            srb.BindBuffer("Target", tgt);
            dispatch(*m_impl->clear_int_stage, *m_impl->clear_int_binding, wg);
        };

        if (m_bound_scene->IsSimulationEnabled()) {
            const uint32_t substep_count = std::max(1u, m_impl->config.num_substep_perstep);
            const uint32_t pos_iters = std::max(1u, m_impl->config.num_iter_persubstep);
            const uint32_t vel_iters = std::max(1u, m_impl->config.num_velocity_iters);

            for (uint32_t ss = 0; ss < substep_count; ++ss) {
                // ====== PreCollision ======
                barrier();

                {
                    auto &srb = m_impl->snapshot_binding->GetShaderResourceBinding();
                    srb.BindBuffer("SrcBuffer", *gpu.rigid_body_center_world_position);
                    srb.BindBuffer("DstBuffer", *m_impl->gpu_substep_start_position);
                    Rhi::PushConstants(cb, *m_impl->snapshot_stage, body_count);
                    dispatch(*m_impl->snapshot_stage, *m_impl->snapshot_binding, body_wg);
                }
                barrier();

                {
                    auto &srb = m_impl->snapshot_binding->GetShaderResourceBinding();
                    srb.BindBuffer("SrcBuffer", *gpu.rigid_body_center_world_rotation);
                    srb.BindBuffer("DstBuffer", *m_impl->gpu_substep_start_orientation);
                    Rhi::PushConstants(cb, *m_impl->snapshot_stage, body_count);
                    dispatch(*m_impl->snapshot_stage, *m_impl->snapshot_binding, body_wg);
                }
                barrier();

                {
                    auto &srb = m_impl->integrate_binding->GetShaderResourceBinding();
                    srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                    srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
                    srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
                    srb.BindBuffer("RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity);
                    srb.BindBuffer("RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity);
                    srb.BindBuffer("RigidBodyMass", *gpu.rigid_body_mass);
                    srb.BindBuffer("RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia);
                    srb.BindBuffer("RigidBodyInertia", *gpu.rigid_body_inertia);
                    srb.BindBuffer("RigidBodyExternalForce", *gpu.rigid_body_external_force);
                    srb.BindBuffer("RigidBodyExternalTorque", *gpu.rigid_body_external_torque);
                    srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
                    Rhi::PushConstants(cb, *m_impl->integrate_stage, m_impl->push_gravity_dt);
                    dispatch(*m_impl->integrate_stage, *m_impl->integrate_binding, body_wg);
                }
                barrier();

                {
                    auto &srb = m_impl->snapshot_binding->GetShaderResourceBinding();
                    srb.BindBuffer("SrcBuffer", *gpu.rigid_body_linear_velocity);
                    srb.BindBuffer("DstBuffer", *m_impl->gpu_pre_contact_linear_vel);
                    Rhi::PushConstants(cb, *m_impl->snapshot_stage, body_count);
                    dispatch(*m_impl->snapshot_stage, *m_impl->snapshot_binding, body_wg);
                }
                barrier();

                {
                    auto &srb = m_impl->snapshot_binding->GetShaderResourceBinding();
                    srb.BindBuffer("SrcBuffer", *gpu.rigid_body_angular_velocity);
                    srb.BindBuffer("DstBuffer", *m_impl->gpu_pre_contact_angular_vel);
                    Rhi::PushConstants(cb, *m_impl->snapshot_stage, body_count);
                    dispatch(*m_impl->snapshot_stage, *m_impl->snapshot_binding, body_wg);
                }
                barrier();

                if (shape_count > 1u && gpu.shape_world_position != nullptr) {
                    auto &srb = m_impl->update_shape_world_pose_binding->GetShaderResourceBinding();
                    srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
                    srb.BindBuffer("ShapeBoundRigidBody", *gpu.shape_bound_rigid_body);
                    srb.BindBuffer("ShapeLocalPosition", *gpu.shape_local_position);
                    srb.BindBuffer("ShapeLocalRotation", *gpu.shape_local_rotation);
                    srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
                    srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
                    srb.BindBuffer("ShapeWorldPosition", *gpu.shape_world_position);
                    srb.BindBuffer("ShapeWorldRotation", *gpu.shape_world_rotation);
                    dispatch(
                        *m_impl->update_shape_world_pose_stage, *m_impl->update_shape_world_pose_binding, shape_wg
                    );
                }

                // ====== Collision Detection ======
                m_impl->broad_detector->Record(cb);
                m_impl->narrow_detector->Record(cb);

                // ====== PostCollision PreIter ======
                barrier();

                dispatch_clear(*m_impl->gpu_position_delta_count, body_count, body_wg);
                barrier();

                dispatch_clear(
                    *m_impl->gpu_contact_lagrange,
                    static_cast<uint32_t>(m_impl->gpu_contact_lagrange->GetSize() / sizeof(float)),
                    (static_cast<uint32_t>(m_impl->gpu_contact_lagrange->GetSize() / sizeof(float)) + 63u) / 64u
                );
                barrier();

                {
                    auto &srb = m_impl->clear_hinge_lagrange_binding->GetShaderResourceBinding();
                    srb.BindBuffer("HingeAxisLagrange", *m_impl->gpu_hinge_axis_lagrange);
                    srb.BindBuffer("HingeAnchorLagrange", *m_impl->gpu_hinge_anchor_lagrange);
                    Rhi::PushConstants(cb, *m_impl->clear_hinge_lagrange_stage, gpu.hinge_joint_count);
                    dispatch(
                        *m_impl->clear_hinge_lagrange_stage,
                        *m_impl->clear_hinge_lagrange_binding,
                        (gpu.hinge_joint_count + 63u) / 64u
                    );
                }
                barrier();

                {
                    auto &srb = m_impl->clear_fixed_lagrange_binding->GetShaderResourceBinding();
                    srb.BindBuffer("FixedRotationLagrange", *m_impl->gpu_fixed_rotation_lagrange);
                    srb.BindBuffer("FixedPositionLagrange", *m_impl->gpu_fixed_position_lagrange);
                    Rhi::PushConstants(cb, *m_impl->clear_fixed_lagrange_stage, gpu.fixed_joint_count);
                    dispatch(
                        *m_impl->clear_fixed_lagrange_stage,
                        *m_impl->clear_fixed_lagrange_binding,
                        (gpu.fixed_joint_count + 63u) / 64u
                    );
                }

                // ====== Position Iterations ======
                for (uint32_t iter = 0; iter < pos_iters; ++iter) {
                    barrier();

                    const auto g = m_bound_scene->GetGpuBuffers();

                    {
                        auto &srb = m_impl->accum_pos_binding->GetShaderResourceBinding();
                        srb.BindBuffer("RigidBodyAlive", *g.rigid_body_alive);
                        srb.BindBuffer("RigidBodyIsKinematic", *g.rigid_body_is_kinematic);
                        srb.BindBuffer("RigidBodyCenterPosition", *g.rigid_body_center_world_position);
                        srb.BindBuffer("RigidBodyCenterRotation", *g.rigid_body_center_world_rotation);
                        srb.BindBuffer("RigidBodyMass", *g.rigid_body_mass);
                        srb.BindBuffer("RigidBodyInverseInertia", *g.rigid_body_inverse_inertia);
                        srb.BindBuffer("RigidBodyInertia", *g.rigid_body_inertia);
                        srb.BindBuffer("ShapeBoundRigidBody", *g.shape_bound_rigid_body);
                        srb.BindBuffer("ShapeLocalPosition", *g.shape_local_position);
                        srb.BindBuffer("ShapeLocalRotation", *g.shape_local_rotation);
                        srb.BindBuffer("ShapeWorldPosition", *g.shape_world_position);
                        srb.BindBuffer("ShapeWorldRotation", *g.shape_world_rotation);
                        srb.BindBuffer("CollisionIds", *m_impl->narrow_detector->GetResultBuffers().collision_ids);
                        srb.BindBuffer(
                            "CollisionNormals", *m_impl->narrow_detector->GetResultBuffers().collision_normals
                        );
                        srb.BindBuffer("ContactPointA", *m_impl->narrow_detector->GetResultBuffers().contact_point_a);
                        srb.BindBuffer("ContactPointB", *m_impl->narrow_detector->GetResultBuffers().contact_point_b);
                        srb.BindBuffer("CollisionCount", *m_impl->narrow_detector->GetResultBuffers().collision_count);
                        srb.BindBuffer("ContactLagrange", *m_impl->gpu_contact_lagrange);
                        srb.BindBuffer("LinearPositionDelta", *m_impl->gpu_linear_position_delta);
                        srb.BindBuffer("AngularPositionDelta", *m_impl->gpu_angular_position_delta);
                        srb.BindBuffer("PositionDeltaCount", *m_impl->gpu_position_delta_count);
                        dispatch(
                            *m_impl->accum_pos_stage,
                            *m_impl->accum_pos_binding,
                            (m_impl->max_contact_point + 63u) / 64u
                        );
                    }
                    barrier();

                    if (g.hinge_joint_count > 0u) {
                        auto &srb = m_impl->accum_hinge_pos_binding->GetShaderResourceBinding();
                        srb.BindBuffer("HingeJoints", *gpu.gpu_hinge_joints);
                        srb.BindBuffer("HingeAxisLagrange", *m_impl->gpu_hinge_axis_lagrange);
                        srb.BindBuffer("HingeAnchorLagrange", *m_impl->gpu_hinge_anchor_lagrange);
                        srb.BindBuffer("HingeJointAlive", *gpu.gpu_hinge_joint_alive);
                        srb.BindBuffer("RigidBodyAlive", *g.rigid_body_alive);
                        srb.BindBuffer("RigidBodyIsKinematic", *g.rigid_body_is_kinematic);
                        srb.BindBuffer("RigidBodyCenterPosition", *g.rigid_body_center_world_position);
                        srb.BindBuffer("RigidBodyCenterRotation", *g.rigid_body_center_world_rotation);
                        srb.BindBuffer("RigidBodyMass", *g.rigid_body_mass);
                        srb.BindBuffer("RigidBodyInverseInertia", *g.rigid_body_inverse_inertia);
                        srb.BindBuffer("RigidBodyInertia", *g.rigid_body_inertia);
                        srb.BindBuffer("LinearPositionDelta", *m_impl->gpu_linear_position_delta);
                        srb.BindBuffer("AngularPositionDelta", *m_impl->gpu_angular_position_delta);
                        srb.BindBuffer("PositionDeltaCount", *m_impl->gpu_position_delta_count);
                        const HingePushParams hinge_params{m_impl->push_gravity_dt, g.hinge_joint_count};
                        Rhi::PushConstants(cb, *m_impl->accum_hinge_pos_stage, hinge_params);
                        dispatch(
                            *m_impl->accum_hinge_pos_stage,
                            *m_impl->accum_hinge_pos_binding,
                            (g.hinge_joint_count + 63u) / 64u
                        );
                    }
                    barrier();

                    if (g.fixed_joint_count > 0u) {
                        auto &srb = m_impl->accum_fixed_pos_binding->GetShaderResourceBinding();
                        srb.BindBuffer("FixedJoints", *gpu.gpu_fixed_joints);
                        srb.BindBuffer("FixedRotationLagrange", *m_impl->gpu_fixed_rotation_lagrange);
                        srb.BindBuffer("FixedPositionLagrange", *m_impl->gpu_fixed_position_lagrange);
                        srb.BindBuffer("FixedJointAlive", *gpu.gpu_fixed_joint_alive);
                        srb.BindBuffer("RigidBodyAlive", *g.rigid_body_alive);
                        srb.BindBuffer("RigidBodyIsKinematic", *g.rigid_body_is_kinematic);
                        srb.BindBuffer("RigidBodyCenterPosition", *g.rigid_body_center_world_position);
                        srb.BindBuffer("RigidBodyCenterRotation", *g.rigid_body_center_world_rotation);
                        srb.BindBuffer("RigidBodyMass", *g.rigid_body_mass);
                        srb.BindBuffer("RigidBodyInverseInertia", *g.rigid_body_inverse_inertia);
                        srb.BindBuffer("RigidBodyInertia", *g.rigid_body_inertia);
                        srb.BindBuffer("LinearPositionDelta", *m_impl->gpu_linear_position_delta);
                        srb.BindBuffer("AngularPositionDelta", *m_impl->gpu_angular_position_delta);
                        srb.BindBuffer("PositionDeltaCount", *m_impl->gpu_position_delta_count);
                        const FixedPushParams fixed_params{m_impl->push_gravity_dt, g.fixed_joint_count};
                        Rhi::PushConstants(cb, *m_impl->accum_fixed_pos_stage, fixed_params);
                        dispatch(
                            *m_impl->accum_fixed_pos_stage,
                            *m_impl->accum_fixed_pos_binding,
                            (g.fixed_joint_count + 63u) / 64u
                        );
                    }
                    barrier();

                    {
                        auto &srb = m_impl->apply_pos_binding->GetShaderResourceBinding();
                        srb.BindBuffer("RigidBodyAlive", *g.rigid_body_alive);
                        srb.BindBuffer("RigidBodyCenterPosition", *g.rigid_body_center_world_position);
                        srb.BindBuffer("RigidBodyCenterRotation", *g.rigid_body_center_world_rotation);
                        srb.BindBuffer("RigidBodyIsKinematic", *g.rigid_body_is_kinematic);
                        srb.BindBuffer("LinearPositionDelta", *m_impl->gpu_linear_position_delta);
                        srb.BindBuffer("AngularPositionDelta", *m_impl->gpu_angular_position_delta);
                        srb.BindBuffer("PositionDeltaCount", *m_impl->gpu_position_delta_count);
                        dispatch(*m_impl->apply_pos_stage, *m_impl->apply_pos_binding, body_wg);
                    }
                }

                // ====== PostPosition ======
                barrier();

                {
                    auto &srb = m_impl->update_vel_binding->GetShaderResourceBinding();
                    srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                    srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
                    srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
                    srb.BindBuffer("RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity);
                    srb.BindBuffer("RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity);
                    srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
                    srb.BindBuffer("SubstepStartPosition", *m_impl->gpu_substep_start_position);
                    srb.BindBuffer("SubstepStartOrientation", *m_impl->gpu_substep_start_orientation);
                    Rhi::PushConstants(cb, *m_impl->update_vel_stage, m_impl->push_gravity_dt);
                    dispatch(*m_impl->update_vel_stage, *m_impl->update_vel_binding, body_wg);
                }

                // ====== Velocity Iterations ======
                for (uint32_t iter = 0; iter < vel_iters; ++iter) {
                    barrier();

                    dispatch_clear(
                        *m_impl->gpu_velocity_delta_count, body_count, (body_count + 63u) / 64u
                    );
                    barrier();

                    {
                        const auto g = m_bound_scene->GetGpuBuffers();
                        auto &srb = m_impl->accum_vel_binding->GetShaderResourceBinding();
                        srb.BindBuffer("CollisionIds", *m_impl->narrow_detector->GetResultBuffers().collision_ids);
                        srb.BindBuffer(
                            "CollisionNormals", *m_impl->narrow_detector->GetResultBuffers().collision_normals
                        );
                        srb.BindBuffer("ContactPointA", *m_impl->narrow_detector->GetResultBuffers().contact_point_a);
                        srb.BindBuffer("ContactPointB", *m_impl->narrow_detector->GetResultBuffers().contact_point_b);
                        srb.BindBuffer("CollisionCount", *m_impl->narrow_detector->GetResultBuffers().collision_count);
                        srb.BindBuffer("ShapeBoundRigidBody", *g.shape_bound_rigid_body);
                        srb.BindBuffer("RigidBodyAlive", *g.rigid_body_alive);
                        srb.BindBuffer("RigidBodyCenterRotation", *g.rigid_body_center_world_rotation);
                        srb.BindBuffer("RigidBodyLinearVelocity", *g.rigid_body_linear_velocity);
                        srb.BindBuffer("RigidBodyAngularVelocity", *g.rigid_body_angular_velocity);
                        srb.BindBuffer("RigidBodyMass", *g.rigid_body_mass);
                        srb.BindBuffer("RigidBodyInverseInertia", *g.rigid_body_inverse_inertia);
                        srb.BindBuffer("RigidBodyDynamicFriction", *g.rigid_body_dynamic_friction);
                        srb.BindBuffer("RigidBodyRestitution", *g.rigid_body_restitution);
                        srb.BindBuffer("RigidBodyIsKinematic", *g.rigid_body_is_kinematic);
                        srb.BindBuffer("PreContactLinearVelocity", *m_impl->gpu_pre_contact_linear_vel);
                        srb.BindBuffer("PreContactAngularVelocity", *m_impl->gpu_pre_contact_angular_vel);
                        srb.BindBuffer("ShapeLocalPosition", *g.shape_local_position);
                        srb.BindBuffer("ShapeLocalRotation", *g.shape_local_rotation);
                        srb.BindBuffer("LinearVelocityDelta", *m_impl->gpu_linear_velocity_delta);
                        srb.BindBuffer("AngularVelocityDelta", *m_impl->gpu_angular_velocity_delta);
                        srb.BindBuffer("VelocityDeltaCount", *m_impl->gpu_velocity_delta_count);
                        srb.BindBuffer("ContactLagrange", *m_impl->gpu_contact_lagrange);
                        Rhi::PushConstants(cb, *m_impl->accum_vel_stage, m_impl->push_gravity_dt);
                        dispatch(
                            *m_impl->accum_vel_stage,
                            *m_impl->accum_vel_binding,
                            (m_impl->max_contact_point + 63u) / 64u
                        );
                    }
                    barrier();

                    {
                        auto &srb = m_impl->apply_vel_binding->GetShaderResourceBinding();
                        srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                        srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
                        srb.BindBuffer("RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity);
                        srb.BindBuffer("RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity);
                        srb.BindBuffer("LinearVelocityDelta", *m_impl->gpu_linear_velocity_delta);
                        srb.BindBuffer("AngularVelocityDelta", *m_impl->gpu_angular_velocity_delta);
                        srb.BindBuffer("VelocityDeltaCount", *m_impl->gpu_velocity_delta_count);
                        dispatch(*m_impl->apply_vel_stage, *m_impl->apply_vel_binding, body_wg);
                    }
                }
            }
        }

        // ====== ModelMatrix ======
        barrier();

        {
            auto &srb = m_impl->model_matrix_binding->GetShaderResourceBinding();
            srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
            srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
            srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
            srb.BindBuffer("ModelMatrices", *gpu.model_matrices);
            dispatch(*m_impl->model_matrix_stage, *m_impl->model_matrix_binding, body_wg);
        }
    }
} // namespace Engine
