#include "XpbdGpuSolver.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/Collision/ConvexCollisionDetector.h>
#include <Physics/Collision/SpatialHashBroadDetector.h>
#include <Physics/PhysicsScene.h>
#include <Render/Memory/ComputeBuffer.h>
#include <Render/Memory/DeviceBuffer.h>
#include <Render/Memory/ShaderParameters/ShaderResourceBinding.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/Compute/ComputeResourceBinding.h>
#include <Render/Pipeline/Compute/ComputeStage.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/SceneDataManager.h>

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
} // namespace

namespace Engine {

    struct XpbdGpuSolver::Impl {
        RenderSystem &render_system;

        bool shaders_loaded = false;
        XpbdConfig config{};
        uint32_t max_contact_point = 0u;

        std::unique_ptr<SpatialHashBroadDetector> broad_detector{};
        std::unique_ptr<ConvexCollisionDetector> narrow_detector{};

        // ---- Compute stages ----
        std::unique_ptr<ComputeStage> clear_int_stage{};
        std::unique_ptr<ComputeStage> snapshot_stage{};
        std::unique_ptr<ComputeStage> update_shape_world_pose_stage{};
        std::unique_ptr<ComputeStage> integrate_stage{};
        std::unique_ptr<ComputeStage> accum_pos_stage{};
        std::unique_ptr<ComputeStage> apply_pos_stage{};
        std::unique_ptr<ComputeStage> update_vel_stage{};
        std::unique_ptr<ComputeStage> accum_vel_stage{};
        std::unique_ptr<ComputeStage> apply_vel_stage{};
        std::unique_ptr<ComputeStage> model_matrix_stage{};
        std::unique_ptr<ComputeStage> clear_hinge_lagrange_stage{};
        std::unique_ptr<ComputeStage> clear_fixed_lagrange_stage{};
        std::unique_ptr<ComputeStage> accum_hinge_pos_stage{};
        std::unique_ptr<ComputeStage> accum_fixed_pos_stage{};

        // ---- Pre-allocated bindings ----
        ComputeResourceBinding *clear_int_binding = nullptr;
        ComputeResourceBinding *snapshot_binding = nullptr;
        ComputeResourceBinding *update_shape_world_pose_binding = nullptr;
        ComputeResourceBinding *integrate_binding = nullptr;
        ComputeResourceBinding *accum_pos_binding = nullptr;
        ComputeResourceBinding *apply_pos_binding = nullptr;
        ComputeResourceBinding *update_vel_binding = nullptr;
        ComputeResourceBinding *accum_vel_binding = nullptr;
        ComputeResourceBinding *apply_vel_binding = nullptr;
        ComputeResourceBinding *model_matrix_binding = nullptr;
        ComputeResourceBinding *clear_hinge_lagrange_binding = nullptr;
        ComputeResourceBinding *clear_fixed_lagrange_binding = nullptr;
        ComputeResourceBinding *accum_hinge_pos_binding = nullptr;
        ComputeResourceBinding *accum_fixed_pos_binding = nullptr;

        // ---- Intermediate GPU buffers ----
        std::unique_ptr<ComputeBuffer> gpu_pre_contact_linear_vel{};
        std::unique_ptr<ComputeBuffer> gpu_pre_contact_angular_vel{};
        std::unique_ptr<ComputeBuffer> gpu_substep_start_position{};
        std::unique_ptr<ComputeBuffer> gpu_substep_start_orientation{};
        std::unique_ptr<ComputeBuffer> gpu_linear_position_delta{};
        std::unique_ptr<ComputeBuffer> gpu_angular_position_delta{};
        std::unique_ptr<ComputeBuffer> gpu_position_delta_count{};
        std::unique_ptr<ComputeBuffer> gpu_linear_velocity_delta{};
        std::unique_ptr<ComputeBuffer> gpu_angular_velocity_delta{};
        std::unique_ptr<ComputeBuffer> gpu_velocity_delta_count{};
        std::unique_ptr<ComputeBuffer> gpu_contact_lagrange{};
        std::unique_ptr<ComputeBuffer> gpu_hinge_axis_lagrange{};
        std::unique_ptr<ComputeBuffer> gpu_hinge_anchor_lagrange{};
        std::unique_ptr<ComputeBuffer> gpu_fixed_rotation_lagrange{};
        std::unique_ptr<ComputeBuffer> gpu_fixed_position_lagrange{};
        std::unique_ptr<ComputeBuffer> gpu_hinge_joint_count_buffer{};
        std::unique_ptr<ComputeBuffer> gpu_fixed_joint_count_buffer{};
        std::unique_ptr<ComputeBuffer> gpu_uniforms{};
        std::unique_ptr<ComputeBuffer> gpu_body_count_buffer{};
        std::unique_ptr<ComputeBuffer> gpu_contact_count_buffer{};
        std::unique_ptr<ComputeBuffer> gpu_shape_slot_count_buffer{};

        explicit Impl(RenderSystem &rs) : render_system(rs) {
        }

        void EnsureBuffer(std::unique_ptr<ComputeBuffer> &buf, size_t bytes, const char *name) {
            const auto &alloc = render_system.GetAllocatorState();
            if (!buf || buf->GetSize() != bytes) {
                buf = ComputeBuffer::CreateUnique(alloc, bytes, false, false, false, false, name);
            }
        }

        void EnsureIntermediateBuffers(
            uint32_t body_count, uint32_t max_contacts, uint32_t hinge_joint_count, uint32_t fixed_joint_count
        ) {
            const auto &alloc = render_system.GetAllocatorState();
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

            {
                size_t sz = sizeof(uint32_t);
                if (!gpu_body_count_buffer || gpu_body_count_buffer->GetSize() != sz) {
                    gpu_body_count_buffer =
                        ComputeBuffer::CreateUnique(alloc, sz, true, false, false, false, "XPBD BodyCount");
                }
                auto *addr = reinterpret_cast<uint32_t *>(gpu_body_count_buffer->GetVMAddress());
                *addr = body_count;
            }
            {
                size_t sz = sizeof(uint32_t);
                if (!gpu_contact_count_buffer || gpu_contact_count_buffer->GetSize() != sz) {
                    gpu_contact_count_buffer =
                        ComputeBuffer::CreateUnique(alloc, sz, true, false, false, false, "XPBD ContactCount");
                }
                auto *addr = reinterpret_cast<uint32_t *>(gpu_contact_count_buffer->GetVMAddress());
                *addr = max_contacts;
            }
            {
                size_t sz = sizeof(uint32_t);
                if (!gpu_shape_slot_count_buffer || gpu_shape_slot_count_buffer->GetSize() != sz) {
                    gpu_shape_slot_count_buffer =
                        ComputeBuffer::CreateUnique(alloc, sz, true, false, false, false, "XPBD ShapeSlotCount");
                }
            }
            {
                size_t sz = sizeof(glm::vec4);
                if (!gpu_uniforms || gpu_uniforms->GetSize() != sz) {
                    gpu_uniforms = ComputeBuffer::CreateUnique(alloc, sz, true, false, false, false, "XPBD Uniforms");
                }
            }
            {
                size_t sz = sizeof(uint32_t);
                if (!gpu_hinge_joint_count_buffer || gpu_hinge_joint_count_buffer->GetSize() != sz) {
                    gpu_hinge_joint_count_buffer =
                        ComputeBuffer::CreateUnique(alloc, sz, true, false, false, false, "XPBD HingeJointCnt");
                }
                auto *addr = reinterpret_cast<uint32_t *>(gpu_hinge_joint_count_buffer->GetVMAddress());
                *addr = hinge_joint_count;
            }
            {
                size_t sz = sizeof(uint32_t);
                if (!gpu_fixed_joint_count_buffer || gpu_fixed_joint_count_buffer->GetSize() != sz) {
                    gpu_fixed_joint_count_buffer =
                        ComputeBuffer::CreateUnique(alloc, sz, true, false, false, false, "XPBD FixedJointCnt");
                }
                auto *addr = reinterpret_cast<uint32_t *>(gpu_fixed_joint_count_buffer->GetVMAddress());
                *addr = fixed_joint_count;
            }
        }

        void EnsureShadersLoaded() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            auto load = [this](const char *path, const char *name) {
                auto spirv = LoadSpirv(path);
                auto stage = std::make_unique<ComputeStage>(render_system);
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

        // ---- Dispatch helpers ----
        void Dispatch(
            vk::CommandBuffer cb,
            ComputeStage &stage,
            ComputeResourceBinding &binding,
            uint32_t wg_x,
            uint32_t wg_y = 1,
            uint32_t wg_z = 1
        ) {
            cb.bindPipeline(vk::PipelineBindPoint::eCompute, stage.GetPipeline());
            cb.bindDescriptorSets(
                vk::PipelineBindPoint::eCompute, stage.GetPipelineLayout(), 0, {binding.GetDescriptorSet(0)}, {}
            );
            cb.dispatch(wg_x, wg_y, wg_z);
        }

        void DispatchClearInt(
            vk::CommandBuffer cb, ComputeResourceBinding &binding, ComputeBuffer &target, ComputeBuffer &count
        ) {
            auto &srb = binding.GetShaderResourceBinding();
            srb.BindBuffer("Target", target);
            srb.BindBuffer("ElemCount", count);
            Dispatch(cb, *clear_int_stage, binding, (count.GetSize() / sizeof(uint32_t) + 63u) / 64u);
        }

        void DispatchSnapshot(vk::CommandBuffer cb, ComputeBuffer &src, ComputeBuffer &dst, uint32_t body_count) {
            auto &srb = snapshot_binding->GetShaderResourceBinding();
            srb.BindBuffer("SrcBuffer", src);
            srb.BindBuffer("DstBuffer", dst);
            srb.BindBuffer("ElemCount", *gpu_body_count_buffer);
            Dispatch(cb, *snapshot_stage, *snapshot_binding, (body_count + 63u) / 64u);
        }
    };

    XpbdGpuSolver::XpbdGpuSolver(RenderSystem &render_system) : m_impl(std::make_unique<Impl>(render_system)) {
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

        *reinterpret_cast<uint32_t *>(m_impl->gpu_shape_slot_count_buffer->GetVMAddress()) = shape_count;

        const float substep_dt =
            m_impl->config.time_step / static_cast<float>(std::max(1u, m_impl->config.num_substep_perstep));
        {
            auto *uniform_addr = reinterpret_cast<glm::vec4 *>(m_impl->gpu_uniforms->GetVMAddress());
            *uniform_addr =
                glm::vec4(m_impl->config.gravity.x, m_impl->config.gravity.y, m_impl->config.gravity.z, substep_dt);
        }

        {
            if (!m_impl->broad_detector) {
                m_impl->broad_detector = std::make_unique<SpatialHashBroadDetector>(m_impl->render_system);
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
                m_impl->narrow_detector = std::make_unique<ConvexCollisionDetector>(m_impl->render_system);
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

    void XpbdGpuSolver::GPUStep(CommandBuffer &command_buffer) {
        const auto gpu = m_bound_scene->GetGpuBuffers();
        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) return;

        const uint32_t body_count = gpu.rigid_body_slot_count;
        const uint32_t shape_count = gpu.shape_slot_count;
        const uint32_t body_wg = (body_count + 63u) / 64u;
        const uint32_t shape_wg = (shape_count + 63u) / 64u;

        auto barrier = [&command_buffer]() {
            command_buffer.GetCommandBuffer().pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});
        };

        auto dispatch =
            [&command_buffer](
                ComputeStage &stage, ComputeResourceBinding &binding, uint32_t x, uint32_t y = 1, uint32_t z = 1
            ) {
                command_buffer.BindComputeStage(stage);
                command_buffer.BindComputeResource(binding);
                command_buffer.DispatchCompute(x, y, z);
            };

        auto dispatch_clear = [this, &dispatch](ComputeBuffer &tgt, ComputeBuffer &cnt, uint32_t wg) {
            auto &srb = m_impl->clear_int_binding->GetShaderResourceBinding();
            srb.BindBuffer("Target", tgt);
            srb.BindBuffer("ElemCount", cnt);
            dispatch(*m_impl->clear_int_stage, *m_impl->clear_int_binding, wg);
        };

        m_impl->render_system.GetSceneDataManager().SetModelMatricesBuffer(gpu.model_matrices);

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
                    srb.BindBuffer("ElemCount", *m_impl->gpu_body_count_buffer);
                    dispatch(*m_impl->snapshot_stage, *m_impl->snapshot_binding, body_wg);
                }
                barrier();

                {
                    auto &srb = m_impl->snapshot_binding->GetShaderResourceBinding();
                    srb.BindBuffer("SrcBuffer", *gpu.rigid_body_center_world_rotation);
                    srb.BindBuffer("DstBuffer", *m_impl->gpu_substep_start_orientation);
                    srb.BindBuffer("ElemCount", *m_impl->gpu_body_count_buffer);
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
                    srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);
                    dispatch(*m_impl->integrate_stage, *m_impl->integrate_binding, body_wg);
                }
                barrier();

                {
                    auto &srb = m_impl->snapshot_binding->GetShaderResourceBinding();
                    srb.BindBuffer("SrcBuffer", *gpu.rigid_body_linear_velocity);
                    srb.BindBuffer("DstBuffer", *m_impl->gpu_pre_contact_linear_vel);
                    srb.BindBuffer("ElemCount", *m_impl->gpu_body_count_buffer);
                    dispatch(*m_impl->snapshot_stage, *m_impl->snapshot_binding, body_wg);
                }
                barrier();

                {
                    auto &srb = m_impl->snapshot_binding->GetShaderResourceBinding();
                    srb.BindBuffer("SrcBuffer", *gpu.rigid_body_angular_velocity);
                    srb.BindBuffer("DstBuffer", *m_impl->gpu_pre_contact_angular_vel);
                    srb.BindBuffer("ElemCount", *m_impl->gpu_body_count_buffer);
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
                m_impl->broad_detector->Record(command_buffer);
                m_impl->narrow_detector->Record(command_buffer);

                // ====== PostCollision PreIter ======
                barrier();

                dispatch_clear(
                    *m_impl->gpu_position_delta_count, *m_impl->gpu_body_count_buffer, body_wg
                );
                barrier();

                dispatch_clear(
                    *m_impl->gpu_contact_lagrange,
                    *m_impl->gpu_contact_count_buffer,
                    (static_cast<uint32_t>(m_impl->gpu_contact_lagrange->GetSize() / sizeof(float)) + 63u) / 64u
                );
                barrier();

                {
                    auto &srb = m_impl->clear_hinge_lagrange_binding->GetShaderResourceBinding();
                    srb.BindBuffer("HingeAxisLagrange", *m_impl->gpu_hinge_axis_lagrange);
                    srb.BindBuffer("HingeAnchorLagrange", *m_impl->gpu_hinge_anchor_lagrange);
                    srb.BindBuffer("HingeJointCount", *m_impl->gpu_hinge_joint_count_buffer);
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
                    srb.BindBuffer("FixedJointCount", *m_impl->gpu_fixed_joint_count_buffer);
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
                        srb.BindBuffer("ShapeSlotCount", *m_impl->gpu_shape_slot_count_buffer);
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
                        srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);
                        dispatch(*m_impl->accum_pos_stage, *m_impl->accum_pos_binding, (m_impl->max_contact_point + 63u) / 64u);
                    }
                    barrier();

                    if (g.hinge_joint_count > 0u) {
                        auto &srb = m_impl->accum_hinge_pos_binding->GetShaderResourceBinding();
                        srb.BindBuffer("HingeJoints", *gpu.gpu_hinge_joints);
                        srb.BindBuffer("HingeJointCount", *m_impl->gpu_hinge_joint_count_buffer);
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
                        srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);
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
                        srb.BindBuffer("FixedJointCount", *m_impl->gpu_fixed_joint_count_buffer);
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
                        srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);
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
                    srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);
                    dispatch(*m_impl->update_vel_stage, *m_impl->update_vel_binding, body_wg);
                }

                // ====== Velocity Iterations ======
                for (uint32_t iter = 0; iter < vel_iters; ++iter) {
                    barrier();

                    dispatch_clear(
                        *m_impl->gpu_velocity_delta_count, *m_impl->gpu_body_count_buffer, (body_count + 63u) / 64u
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
                        srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);
                        dispatch(*m_impl->accum_vel_stage, *m_impl->accum_vel_binding, (m_impl->max_contact_point + 63u) / 64u);
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
            srb.BindBuffer("ElemCount", *m_impl->gpu_body_count_buffer);
            dispatch(*m_impl->model_matrix_stage, *m_impl->model_matrix_binding, body_wg);
        }
    }
} // namespace Engine
