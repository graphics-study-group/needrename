#include "XPBDGpuSolver.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/Collision/ConvexCollisionDetector.h>
#include <Physics/PhysicsScene.h>
#include <Render/Memory/ComputeBuffer.h>
#include <Render/Memory/DeviceBuffer.h>
#include <Render/Memory/MemoryAccessTypes.h>
#include <Render/Memory/ShaderParameters/ShaderResourceBinding.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/Compute/ComputeResourceBinding.h>
#include <Render/Pipeline/Compute/ComputeStage.h>
#include <Render/Pipeline/RenderGraph/RGAttachmentDesc.h>
#include <Render/Pipeline/RenderGraph/RenderGraphBuilder.h>
#include <Render/Pipeline/RenderGraph/RenderGraphPass.h>
#include <Render/RenderSystem.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
    std::vector<uint32_t> LoadPhysicsSpirv(const char *relative_path) {
        std::filesystem::path full = std::filesystem::path(ENGINE_PHYSICS_SPIRV_DIR) / relative_path;
        std::ifstream file(full, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open physics SPIR-V: " + full.string());
        }
        const auto size = static_cast<size_t>(file.tellg());
        if (size == 0u || size % sizeof(uint32_t) != 0u) {
            throw std::runtime_error("Invalid physics SPIR-V size: " + full.string());
        }
        std::vector<uint32_t> words(size / sizeof(uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size));
        return words;
    }
} // namespace

namespace Engine {

    // ---- PImpl ----

    struct XPBDGpuSolver::Impl {
        RenderSystem &render_system;

        bool initialized = false;
        XpbdConfig config{};

        // Cached counts for lazy reallocation.
        uint32_t cached_body_count = 0;
        uint32_t cached_max_contacts = 0;
        uint32_t cached_shape_count = 0;

        // ---- Owned collision detector ----
        std::unique_ptr<ConvexCollisionDetector> collision_detector{};

        // ---- Compute stages (one per shader) ----

        std::unique_ptr<ComputeStage> clear_int_stage{};
        std::vector<uint32_t> clear_int_spirv{};

        std::unique_ptr<ComputeStage> snapshot_stage{};
        std::vector<uint32_t> snapshot_spirv{};

        std::unique_ptr<ComputeStage> update_shape_world_pose_stage{};
        std::vector<uint32_t> update_shape_world_pose_spirv{};

        std::unique_ptr<ComputeStage> integrate_stage{};
        std::vector<uint32_t> integrate_spirv{};

        std::unique_ptr<ComputeStage> accum_pos_stage{};
        std::vector<uint32_t> accum_pos_spirv{};

        std::unique_ptr<ComputeStage> apply_pos_stage{};
        std::vector<uint32_t> apply_pos_spirv{};

        std::unique_ptr<ComputeStage> update_vel_stage{};
        std::vector<uint32_t> update_vel_spirv{};

        std::unique_ptr<ComputeStage> accum_vel_stage{};
        std::vector<uint32_t> accum_vel_spirv{};

        std::unique_ptr<ComputeStage> apply_vel_stage{};
        std::vector<uint32_t> apply_vel_spirv{};

        // ---- Model matrix (unchanged) ----
        std::unique_ptr<ComputeStage> model_matrix_stage{};
        std::vector<uint32_t> model_matrix_spirv{};

        // ---- Intermediate GPU buffers (owned) ----

        // Snapshots (per-body, vec4).
        std::unique_ptr<ComputeBuffer> gpu_pre_gravity_position{};
        std::unique_ptr<ComputeBuffer> gpu_pre_gravity_orientation{};
        std::unique_ptr<ComputeBuffer> gpu_pre_contact_linear_vel{};
        std::unique_ptr<ComputeBuffer> gpu_pre_contact_angular_vel{};
        std::unique_ptr<ComputeBuffer> gpu_substep_start_position{};
        std::unique_ptr<ComputeBuffer> gpu_substep_start_orientation{};

        // Jacobi delta accumulators (per-body, 3 ints each for float atomic-add).
        std::unique_ptr<ComputeBuffer> gpu_linear_position_delta{};
        std::unique_ptr<ComputeBuffer> gpu_angular_position_delta{};
        std::unique_ptr<ComputeBuffer> gpu_position_delta_count{};

        std::unique_ptr<ComputeBuffer> gpu_linear_velocity_delta{};
        std::unique_ptr<ComputeBuffer> gpu_angular_velocity_delta{};
        std::unique_ptr<ComputeBuffer> gpu_velocity_delta_count{};

        // Lagrange multipliers (per-contact, float as int).
        std::unique_ptr<ComputeBuffer> gpu_contact_lagrange{};

        // Zero-filled buffer for lagrange memset (size = bytes).
        std::unique_ptr<ComputeBuffer> gpu_zero_buffer{};

        // Uniform SSBO: vec4(gravity.xyz, dt). Host-visible, written once.
        std::unique_ptr<ComputeBuffer> gpu_uniforms{};

        // Element-count buffers (single uint each, host-visible).
        std::unique_ptr<ComputeBuffer> gpu_body_count_buffer{};
        std::unique_ptr<ComputeBuffer> gpu_contact_count_buffer{};

        explicit Impl(RenderSystem &rs) : render_system(rs) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        // -----------------------------------------------------------------------
        // Buffer helpers
        // -----------------------------------------------------------------------

        void EnsureBuffer(std::unique_ptr<ComputeBuffer> &buf, size_t bytes, const char *name) {
            const auto &alloc = render_system.GetAllocatorState();
            if (!buf || buf->GetSize() != bytes) {
                buf = ComputeBuffer::CreateUnique(alloc, bytes, false, false, false, false, name);
            }
        }

        void EnsureIntermediateBuffers(uint32_t body_count, uint32_t max_contacts) {
            const auto &alloc = render_system.GetAllocatorState();
            size_t body_bytes = static_cast<size_t>(body_count) * sizeof(glm::vec4);
            size_t body_int3 = static_cast<size_t>(body_count) * 3 * sizeof(int);
            size_t body_int1 = static_cast<size_t>(body_count) * sizeof(int);
            size_t lagrange_bytes = static_cast<size_t>(max_contacts) * sizeof(int);

            EnsureBuffer(gpu_pre_gravity_position, body_bytes, "XPBD PreGravityPos");
            EnsureBuffer(gpu_pre_gravity_orientation, body_bytes, "XPBD PreGravityOri");
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
            EnsureBuffer(gpu_contact_lagrange, lagrange_bytes, "XPBD ContactLagrange");

            // Single-uint buffer for snapshot element count.
            {
                size_t sz = sizeof(uint32_t);
                if (!gpu_body_count_buffer || gpu_body_count_buffer->GetSize() != sz) {
                    gpu_body_count_buffer =
                        ComputeBuffer::CreateUnique(alloc, sz, true, false, false, false, "XPBD BodyCount");
                }
                auto *addr = reinterpret_cast<uint32_t *>(gpu_body_count_buffer->GetVMAddress());
                *addr = body_count;
            }

            // Single-uint buffer for contacts clear count.
            {
                size_t sz = sizeof(uint32_t);
                if (!gpu_contact_count_buffer || gpu_contact_count_buffer->GetSize() != sz) {
                    gpu_contact_count_buffer =
                        ComputeBuffer::CreateUnique(alloc, sz, true, false, false, false, "XPBD ContactCount");
                }
                auto *addr = reinterpret_cast<uint32_t *>(gpu_contact_count_buffer->GetVMAddress());
                *addr = max_contacts;
            }

            // Uniform SSBO: vec4(gravity, dt). Recreate if missing.
            {
                size_t sz = sizeof(glm::vec4);
                if (!gpu_uniforms || gpu_uniforms->GetSize() != sz) {
                    gpu_uniforms = ComputeBuffer::CreateUnique(alloc, sz, true, false, false, false, "XPBD Uniforms");
                }
            }

            // Zero buffer sized to max(max_contacts, body_count) * 4 bytes.
            {
                size_t sz = std::max(max_contacts * sizeof(int), body_count * sizeof(glm::vec4));
                if (!gpu_zero_buffer || gpu_zero_buffer->GetSize() < sz) {
                    gpu_zero_buffer =
                        ComputeBuffer::CreateUnique(alloc, sz, false, false, false, false, "XPBD ZeroBuf");
                }
            }
        }

        void EnsureCollisionDetector(uint32_t shape_count) {
            if (shape_count <= 1u) {
                collision_detector.reset();
                cached_shape_count = shape_count;
                return;
            }
            // Each collision pair can produce up to 5 contact points (4 perturbation + 1 MPR fallback).
            uint32_t max_pairs = std::min((shape_count * (shape_count - 1u)) / 2u * 5u, config.max_contact_points);
            if (!collision_detector || shape_count != cached_shape_count) {
                collision_detector =
                    std::make_unique<ConvexCollisionDetector>(render_system, max_pairs, config.contact_margin);
                cached_shape_count = shape_count;
            }
        }

        // -----------------------------------------------------------------------
        // Shader loading
        // -----------------------------------------------------------------------

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            clear_int_spirv = LoadPhysicsSpirv("solver/XPBDSolver/clear_int_buffer.comp.spv");
            clear_int_stage = std::make_unique<ComputeStage>(render_system);
            clear_int_stage->Instantiate(clear_int_spirv, "XPBD Clear Int Buffer");

            snapshot_spirv = LoadPhysicsSpirv("solver/XPBDSolver/snapshot_position.comp.spv");
            snapshot_stage = std::make_unique<ComputeStage>(render_system);
            snapshot_stage->Instantiate(snapshot_spirv, "XPBD Snapshot Copy");

            update_shape_world_pose_spirv = LoadPhysicsSpirv("solver/XPBDSolver/update_shape_world_pose.comp.spv");
            update_shape_world_pose_stage = std::make_unique<ComputeStage>(render_system);
            update_shape_world_pose_stage->Instantiate(update_shape_world_pose_spirv, "XPBD Update Shape World Pose");

            integrate_spirv = LoadPhysicsSpirv("solver/XPBDSolver/integrate_forces.comp.spv");
            integrate_stage = std::make_unique<ComputeStage>(render_system);
            integrate_stage->Instantiate(integrate_spirv, "XPBD Integrate Forces");

            accum_pos_spirv = LoadPhysicsSpirv("solver/XPBDSolver/accumulate_contact_position.comp.spv");
            accum_pos_stage = std::make_unique<ComputeStage>(render_system);
            accum_pos_stage->Instantiate(accum_pos_spirv, "XPBD Accum Pos Deltas");

            apply_pos_spirv = LoadPhysicsSpirv("solver/XPBDSolver/apply_body_position_deltas.comp.spv");
            apply_pos_stage = std::make_unique<ComputeStage>(render_system);
            apply_pos_stage->Instantiate(apply_pos_spirv, "XPBD Apply Pos Deltas");

            update_vel_spirv = LoadPhysicsSpirv("solver/XPBDSolver/update_velocities_from_pose.comp.spv");
            update_vel_stage = std::make_unique<ComputeStage>(render_system);
            update_vel_stage->Instantiate(update_vel_spirv, "XPBD Update Velocities");

            accum_vel_spirv = LoadPhysicsSpirv("solver/XPBDSolver/accumulate_contact_velocity.comp.spv");
            accum_vel_stage = std::make_unique<ComputeStage>(render_system);
            accum_vel_stage->Instantiate(accum_vel_spirv, "XPBD Accum Vel Deltas");

            apply_vel_spirv = LoadPhysicsSpirv("solver/XPBDSolver/apply_body_velocity_deltas.comp.spv");
            apply_vel_stage = std::make_unique<ComputeStage>(render_system);
            apply_vel_stage->Instantiate(apply_vel_spirv, "XPBD Apply Vel Deltas");

            model_matrix_spirv = LoadPhysicsSpirv("solver/XPBDSolver/model_matrix.comp.spv");
            model_matrix_stage = std::make_unique<ComputeStage>(render_system);
            model_matrix_stage->Instantiate(model_matrix_spirv, "XPBD Model Matrix");
        }

        // -----------------------------------------------------------------------
        // Single snapshot pass helper
        // -----------------------------------------------------------------------

        void AddSnapshotPass(
            RenderGraphBuilder &builder, const ComputeBuffer &src, const ComputeBuffer &dst, const char *name
        ) {
            auto src_handle = builder.ImportExternalResource(src, {MemoryAccessTypeBufferBits::None});
            auto dst_handle = builder.ImportExternalResource(dst, {MemoryAccessTypeBufferBits::None});
            auto cnt_handle =
                builder.ImportExternalResource(*gpu_body_count_buffer, {MemoryAccessTypeBufferBits::None});

            auto *binding = &snapshot_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("SrcBuffer", src);
            srb.BindBuffer("DstBuffer", dst);
            srb.BindBuffer("ElemCount", *gpu_body_count_buffer);

            auto *stage = snapshot_stage.get();
            uint32_t wg = (cached_body_count + 63u) / 64u;

            builder.AddPass(
                RenderGraphPassBuilder{render_system}
                    .SetName(name)
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(src_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .UseBuffer(dst_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                    .UseBuffer(cnt_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .SetPassFunction([stage, binding, wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }
    };

    // =======================================================================
    // Public API
    // =======================================================================

    XPBDGpuSolver::XPBDGpuSolver(RenderSystem &render_system) : m_impl(std::make_unique<Impl>(render_system)) {
    }

    XPBDGpuSolver::~XPBDGpuSolver() = default;

    bool XPBDGpuSolver::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    void XPBDGpuSolver::SetConfig(const XpbdConfig &config) noexcept {
        m_impl->config = config;
    }

    const XpbdConfig &XPBDGpuSolver::GetConfig() const noexcept {
        return m_impl->config;
    }

    void XPBDGpuSolver::Step(
        RenderGraphBuilder &builder, PhysicsScene &physics_scene, RGBufferHandle external_model_matrices_handle
    ) {
        const auto gpu = physics_scene.GetGpuBuffers();

        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_center_world_position == nullptr
            || gpu.rigid_body_slot_count == 0u) {
            return;
        }

        // ---- Lazy initialization ----
        m_impl->EnsureInitialized();
        m_impl->EnsureCollisionDetector(gpu.shape_slot_count);

        // Compute max_contacts from shape count (up to 5 manifold points per pair:
        // 4 perturbation + optionally 1 MPR fallback).
        const uint32_t shape_count = gpu.shape_slot_count;
        const uint32_t max_pairs = shape_count > 1u ? (shape_count * (shape_count - 1u)) / 2u : 0u;
        const uint32_t max_contacts = std::max(1u, max_pairs * 5u);

        // Raw pointer for pass lambdas — evaluated at dispatch time each frame.
        auto *pscene = &physics_scene;

        const uint32_t body_count = gpu.rigid_body_slot_count;

        // Recreate intermediate buffers if sizes changed.
        if (body_count != m_impl->cached_body_count || max_contacts != m_impl->cached_max_contacts) {
            m_impl->EnsureIntermediateBuffers(body_count, max_contacts);
            m_impl->cached_body_count = body_count;
            m_impl->cached_max_contacts = max_contacts;
        }

        const uint32_t body_wg = (body_count + 63u) / 64u;
        const uint32_t contact_wg = (max_contacts + 63u) / 64u;
        const uint32_t shape_wg = (gpu.shape_slot_count + 63u) / 64u;

        // Pre-import all body buffers (reused across passes).
        auto alive_h = builder.ImportExternalResource(*gpu.rigid_body_alive, {MemoryAccessTypeBufferBits::None});
        auto pos_h =
            builder.ImportExternalResource(*gpu.rigid_body_center_world_position, {MemoryAccessTypeBufferBits::None});
        auto rot_h =
            builder.ImportExternalResource(*gpu.rigid_body_center_world_rotation, {MemoryAccessTypeBufferBits::None});
        auto linvel_h =
            builder.ImportExternalResource(*gpu.rigid_body_linear_velocity, {MemoryAccessTypeBufferBits::None});
        auto angvel_h =
            builder.ImportExternalResource(*gpu.rigid_body_angular_velocity, {MemoryAccessTypeBufferBits::None});
        auto mass_h = builder.ImportExternalResource(*gpu.rigid_body_mass, {MemoryAccessTypeBufferBits::None});
        auto inv_inertia_h =
            builder.ImportExternalResource(*gpu.rigid_body_inverse_inertia, {MemoryAccessTypeBufferBits::None});
        auto inertia_h = builder.ImportExternalResource(*gpu.rigid_body_inertia, {MemoryAccessTypeBufferBits::None});
        auto extforce_h =
            builder.ImportExternalResource(*gpu.rigid_body_external_force, {MemoryAccessTypeBufferBits::None});
        auto exttorque_h =
            builder.ImportExternalResource(*gpu.rigid_body_external_torque, {MemoryAccessTypeBufferBits::None});
        auto kinematic_h =
            builder.ImportExternalResource(*gpu.rigid_body_is_kinematic, {MemoryAccessTypeBufferBits::None});
        auto dynfric_h =
            builder.ImportExternalResource(*gpu.rigid_body_dynamic_friction, {MemoryAccessTypeBufferBits::None});
        auto restitution_h =
            builder.ImportExternalResource(*gpu.rigid_body_restitution, {MemoryAccessTypeBufferBits::None});

        // Pre-import shape world/local buffers (read/write by shape world update pass).
        auto shape_alive_h = builder.ImportExternalResource(*gpu.shape_alive, {MemoryAccessTypeBufferBits::None});
        auto shape_local_pos_h =
            builder.ImportExternalResource(*gpu.shape_local_position, {MemoryAccessTypeBufferBits::None});
        auto shape_local_rot_h =
            builder.ImportExternalResource(*gpu.shape_local_rotation, {MemoryAccessTypeBufferBits::None});
        auto shape_world_pos_h =
            builder.ImportExternalResource(*gpu.shape_world_position, {MemoryAccessTypeBufferBits::None});
        auto shape_world_rot_h =
            builder.ImportExternalResource(*gpu.shape_world_rotation, {MemoryAccessTypeBufferBits::None});

        // Shape→body mapping.
        auto shape2body_h =
            builder.ImportExternalResource(*gpu.shape_bound_rigid_body, {MemoryAccessTypeBufferBits::None});

        // Pre-import intermediate buffers.
        auto pregrav_pos_h =
            builder.ImportExternalResource(*m_impl->gpu_pre_gravity_position, {MemoryAccessTypeBufferBits::None});
        auto pregrav_ori_h =
            builder.ImportExternalResource(*m_impl->gpu_pre_gravity_orientation, {MemoryAccessTypeBufferBits::None});
        auto precont_lv_h =
            builder.ImportExternalResource(*m_impl->gpu_pre_contact_linear_vel, {MemoryAccessTypeBufferBits::None});
        auto precont_av_h =
            builder.ImportExternalResource(*m_impl->gpu_pre_contact_angular_vel, {MemoryAccessTypeBufferBits::None});
        auto ssp_pos_h =
            builder.ImportExternalResource(*m_impl->gpu_substep_start_position, {MemoryAccessTypeBufferBits::None});
        auto ssp_ori_h =
            builder.ImportExternalResource(*m_impl->gpu_substep_start_orientation, {MemoryAccessTypeBufferBits::None});
        auto lindelta_h =
            builder.ImportExternalResource(*m_impl->gpu_linear_position_delta, {MemoryAccessTypeBufferBits::None});
        auto angdelta_h =
            builder.ImportExternalResource(*m_impl->gpu_angular_position_delta, {MemoryAccessTypeBufferBits::None});
        auto cntdelta_h =
            builder.ImportExternalResource(*m_impl->gpu_position_delta_count, {MemoryAccessTypeBufferBits::None});
        auto linveldelta_h =
            builder.ImportExternalResource(*m_impl->gpu_linear_velocity_delta, {MemoryAccessTypeBufferBits::None});
        auto angveldelta_h =
            builder.ImportExternalResource(*m_impl->gpu_angular_velocity_delta, {MemoryAccessTypeBufferBits::None});
        auto velcntdelta_h =
            builder.ImportExternalResource(*m_impl->gpu_velocity_delta_count, {MemoryAccessTypeBufferBits::None});
        auto lagrange_h =
            builder.ImportExternalResource(*m_impl->gpu_contact_lagrange, {MemoryAccessTypeBufferBits::None});

        using AT = MemoryAccessTypeBufferBits;
        const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
        const MemoryAccessTypeBuffer RW{AT::ShaderRandomRead, AT::ShaderRandomWrite};
        const MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};

        // ===================================================================
        // Substep loop
        // ===================================================================
        const uint32_t substep_count = std::max(1u, m_impl->config.num_substep_perstep);
        const float substep_dt = m_impl->config.time_step / static_cast<float>(substep_count);
        const glm::vec4 gravity_dt(
            m_impl->config.gravity.x, m_impl->config.gravity.y, m_impl->config.gravity.z, substep_dt
        );

        // Write uniforms to host-visible buffer.
        {
            auto *uniform_addr = reinterpret_cast<glm::vec4 *>(m_impl->gpu_uniforms->GetVMAddress());
            *uniform_addr = gravity_dt;
        }

        // Pre-import uniform buffer.
        auto uniforms_h = builder.ImportExternalResource(*m_impl->gpu_uniforms, {MemoryAccessTypeBufferBits::None});

        for (uint32_t ss = 0; ss < substep_count; ++ss) {
            // --- Pass: Snapshot pre-gravity pose ---
            m_impl->AddSnapshotPass(
                builder,
                *gpu.rigid_body_center_world_position,
                *m_impl->gpu_pre_gravity_position,
                "XPBD Snap PreGravPos"
            );
            m_impl->AddSnapshotPass(
                builder,
                *gpu.rigid_body_center_world_rotation,
                *m_impl->gpu_pre_gravity_orientation,
                "XPBD Snap PreGravOri"
            );

            // --- Pass: Integrate forces ---
            {
                auto *binding = &m_impl->integrate_stage->AllocateResourceBinding();
                auto &srb = binding->GetShaderResourceBinding();
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

                auto *stage = m_impl->integrate_stage.get();
                builder.AddPass(
                    RenderGraphPassBuilder{m_impl->render_system}
                        .SetName("XPBD Integrate Forces")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(pos_h, RW)
                        .UseBuffer(rot_h, RW)
                        .UseBuffer(linvel_h, RW)
                        .UseBuffer(angvel_h, RW)
                        .UseBuffer(alive_h, RR)
                        .UseBuffer(kinematic_h, RR)
                        .UseBuffer(mass_h, RR)
                        .UseBuffer(inv_inertia_h, RR)
                        .UseBuffer(inertia_h, RR)
                        .UseBuffer(extforce_h, RR)
                        .UseBuffer(exttorque_h, RR)
                        .UseBuffer(uniforms_h, RR)
                        .SetPassFunction(
                            [stage, binding, body_wg, pscene](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!pscene->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding);
                                cb.DispatchCompute(body_wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // --- Pass: Snapshot pre-contact velocities ---
            m_impl->AddSnapshotPass(
                builder,
                *gpu.rigid_body_linear_velocity,
                *m_impl->gpu_pre_contact_linear_vel,
                "XPBD Snap PreContactLinVel"
            );
            m_impl->AddSnapshotPass(
                builder,
                *gpu.rigid_body_angular_velocity,
                *m_impl->gpu_pre_contact_angular_vel,
                "XPBD Snap PreContactAngVel"
            );

            // --- Pass: Snapshot substep-start pose ---
            m_impl->AddSnapshotPass(
                builder,
                *gpu.rigid_body_center_world_position,
                *m_impl->gpu_substep_start_position,
                "XPBD Snap SubstepStartPos"
            );
            m_impl->AddSnapshotPass(
                builder,
                *gpu.rigid_body_center_world_rotation,
                *m_impl->gpu_substep_start_orientation,
                "XPBD Snap SubstepStartOri"
            );

            // --- Pass: Update shape world poses ---
            if (gpu.shape_slot_count > 1u && gpu.shape_world_position != nullptr) {
                auto *sw_binding = &m_impl->update_shape_world_pose_stage->AllocateResourceBinding();
                auto &sw_srb = sw_binding->GetShaderResourceBinding();
                sw_srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
                sw_srb.BindBuffer("ShapeBoundRigidBody", *gpu.shape_bound_rigid_body);
                sw_srb.BindBuffer("ShapeLocalPosition", *gpu.shape_local_position);
                sw_srb.BindBuffer("ShapeLocalRotation", *gpu.shape_local_rotation);
                sw_srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
                sw_srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
                sw_srb.BindBuffer("ShapeWorldPosition", *gpu.shape_world_position);
                sw_srb.BindBuffer("ShapeWorldRotation", *gpu.shape_world_rotation);

                auto *sw_stage = m_impl->update_shape_world_pose_stage.get();
                builder.AddPass(
                    RenderGraphPassBuilder{m_impl->render_system}
                        .SetName("XPBD Update Shape World Pose")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(shape_alive_h, RR)
                        .UseBuffer(shape2body_h, RR)
                        .UseBuffer(shape_local_pos_h, RR)
                        .UseBuffer(shape_local_rot_h, RR)
                        .UseBuffer(pos_h, RR)
                        .UseBuffer(rot_h, RR)
                        .UseBuffer(shape_world_pos_h, RW)
                        .UseBuffer(shape_world_rot_h, RW)
                        .SetPassFunction(
                            [sw_stage, sw_binding, shape_wg, pscene](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!pscene->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*sw_stage);
                                cb.BindComputeResource(*sw_binding);
                                cb.DispatchCompute(shape_wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // --- Pass: Collision detection (pair gen + MPR) ---
            // Buffers are created lazily on first Step() call inside the detector.
            if (m_impl->collision_detector) {
                m_impl->collision_detector->Step(builder, physics_scene);
            }

            // Import collision result buffers from the internal detector (now
            // guaranteed to exist after the detector's first Step() above).
            auto cr = m_impl->collision_detector ? m_impl->collision_detector->GetCollisionResultBuffers()
                                                 : CollisionResultBuffers{};
            RGBufferHandle coll_ids_h{}, coll_normals_h{}, coll_pta_h{}, coll_ptb_h{}, coll_cnt_h{};
            if (cr.collision_ids != nullptr) {
                coll_ids_h = builder.ImportExternalResource(*cr.collision_ids, {MemoryAccessTypeBufferBits::None});
                coll_normals_h =
                    builder.ImportExternalResource(*cr.collision_normals, {MemoryAccessTypeBufferBits::None});
                coll_pta_h = builder.ImportExternalResource(*cr.contact_point_a, {MemoryAccessTypeBufferBits::None});
                coll_ptb_h = builder.ImportExternalResource(*cr.contact_point_b, {MemoryAccessTypeBufferBits::None});
                coll_cnt_h = builder.ImportExternalResource(*cr.collision_count, {MemoryAccessTypeBufferBits::None});
            }

            // --- Pass: Memset lagrange to zero ---
            {
                auto *binding = &m_impl->clear_int_stage->AllocateResourceBinding();
                auto &srb = binding->GetShaderResourceBinding();
                srb.BindBuffer("Target", *m_impl->gpu_contact_lagrange);
                srb.BindBuffer("ElemCount", *m_impl->gpu_contact_count_buffer);

                auto *stage = m_impl->clear_int_stage.get();
                builder.AddPass(
                    RenderGraphPassBuilder{m_impl->render_system}
                        .SetName("XPBD Memset Lagrange")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(lagrange_h, WW)
                        .SetPassFunction([stage, binding, contact_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                            cb.BindComputeStage(*stage);
                            cb.BindComputeResource(*binding);
                            cb.DispatchCompute(contact_wg, 1, 1);
                        })
                        .Get()
                );
            }

            // ================================================================
            // Position solve iterations
            // ================================================================
            const uint32_t pos_iters = std::max(1u, m_impl->config.num_iter_persubstep);
            for (uint32_t iter = 0; iter < pos_iters; ++iter) {
                // Accumulate contact position deltas.
                {
                    auto *binding = &m_impl->accum_pos_stage->AllocateResourceBinding();
                    auto &srb = binding->GetShaderResourceBinding();
                    srb.BindBuffer("CollisionIds", *cr.collision_ids);
                    srb.BindBuffer("CollisionNormals", *cr.collision_normals);
                    srb.BindBuffer("ContactPointA", *cr.contact_point_a);
                    srb.BindBuffer("ContactPointB", *cr.contact_point_b);
                    srb.BindBuffer("CollisionCount", *cr.collision_count);
                    srb.BindBuffer("ShapeBoundRigidBody", *gpu.shape_bound_rigid_body);
                    srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                    srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
                    srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
                    srb.BindBuffer("RigidBodyMass", *gpu.rigid_body_mass);
                    srb.BindBuffer("RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia);
                    srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
                    srb.BindBuffer("SubstepStartPosition", *m_impl->gpu_substep_start_position);
                    srb.BindBuffer("SubstepStartOrientation", *m_impl->gpu_substep_start_orientation);
                    srb.BindBuffer("LinearPositionDeltaI", *m_impl->gpu_linear_position_delta);
                    srb.BindBuffer("AngularPositionDeltaI", *m_impl->gpu_angular_position_delta);
                    srb.BindBuffer("PositionDeltaCount", *m_impl->gpu_position_delta_count);
                    srb.BindBuffer("ContactLagrange", *m_impl->gpu_contact_lagrange);

                    auto *stage = m_impl->accum_pos_stage.get();
                    builder.AddPass(
                        RenderGraphPassBuilder{m_impl->render_system}
                            .SetName("XPBD Accum Contact Pos")
                            .SetAffinity(RenderGraphPassAffinity::Compute)
                            .UseBuffer(coll_ids_h, RR)
                            .UseBuffer(coll_normals_h, RR)
                            .UseBuffer(coll_pta_h, RR)
                            .UseBuffer(coll_ptb_h, RR)
                            .UseBuffer(coll_cnt_h, RR)
                            .UseBuffer(shape2body_h, RR)
                            .UseBuffer(pos_h, RR)
                            .UseBuffer(rot_h, RR)
                            .UseBuffer(alive_h, RR)
                            .UseBuffer(kinematic_h, RR)
                            .UseBuffer(mass_h, RR)
                            .UseBuffer(inv_inertia_h, RR)
                            .UseBuffer(ssp_pos_h, RR)
                            .UseBuffer(ssp_ori_h, RR)
                            .UseBuffer(lindelta_h, RW)
                            .UseBuffer(angdelta_h, RW)
                            .UseBuffer(cntdelta_h, RW)
                            .UseBuffer(lagrange_h, RW)
                            .SetPassFunction(
                                [stage, binding, contact_wg, pscene](CommandBuffer &cb, const RenderGraph &) -> void {
                                    if (!pscene->IsSimulationEnabled()) return;
                                    cb.BindComputeStage(*stage);
                                    cb.BindComputeResource(*binding);
                                    cb.DispatchCompute(contact_wg, 1, 1);
                                }
                            )
                            .Get()
                    );
                }

                // Apply body position deltas.
                {
                    auto *binding = &m_impl->apply_pos_stage->AllocateResourceBinding();
                    auto &srb = binding->GetShaderResourceBinding();
                    srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                    srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
                    srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
                    srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
                    srb.BindBuffer("LinearPositionDeltaI", *m_impl->gpu_linear_position_delta);
                    srb.BindBuffer("AngularPositionDeltaI", *m_impl->gpu_angular_position_delta);
                    srb.BindBuffer("PositionDeltaCount", *m_impl->gpu_position_delta_count);

                    auto *stage = m_impl->apply_pos_stage.get();
                    builder.AddPass(
                        RenderGraphPassBuilder{m_impl->render_system}
                            .SetName("XPBD Apply Body Pos")
                            .SetAffinity(RenderGraphPassAffinity::Compute)
                            .UseBuffer(pos_h, RW)
                            .UseBuffer(rot_h, RW)
                            .UseBuffer(alive_h, RR)
                            .UseBuffer(kinematic_h, RR)
                            .UseBuffer(lindelta_h, RW)
                            .UseBuffer(angdelta_h, RW)
                            .UseBuffer(cntdelta_h, RW)
                            .SetPassFunction(
                                [stage, binding, body_wg, pscene](CommandBuffer &cb, const RenderGraph &) -> void {
                                    if (!pscene->IsSimulationEnabled()) return;
                                    cb.BindComputeStage(*stage);
                                    cb.BindComputeResource(*binding);
                                    cb.DispatchCompute(body_wg, 1, 1);
                                }
                            )
                            .Get()
                    );
                }
            } // position iterations

            // --- Pass: Update velocities from pose delta ---
            {
                auto *binding = &m_impl->update_vel_stage->AllocateResourceBinding();
                auto &srb = binding->GetShaderResourceBinding();
                srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
                srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
                srb.BindBuffer("RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity);
                srb.BindBuffer("RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity);
                srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
                srb.BindBuffer("PreGravityPosition", *m_impl->gpu_pre_gravity_position);
                srb.BindBuffer("PreGravityOrientation", *m_impl->gpu_pre_gravity_orientation);
                srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);

                auto *stage = m_impl->update_vel_stage.get();
                builder.AddPass(
                    RenderGraphPassBuilder{m_impl->render_system}
                        .SetName("XPBD Update Velocities")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(linvel_h, RW)
                        .UseBuffer(angvel_h, RW)
                        .UseBuffer(pos_h, RR)
                        .UseBuffer(rot_h, RR)
                        .UseBuffer(alive_h, RR)
                        .UseBuffer(kinematic_h, RR)
                        .UseBuffer(pregrav_pos_h, RR)
                        .UseBuffer(pregrav_ori_h, RR)
                        .UseBuffer(uniforms_h, RR)
                        .SetPassFunction(
                            [stage, binding, body_wg, pscene](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!pscene->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding);
                                cb.DispatchCompute(body_wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // ================================================================
            // Velocity solve iterations
            // ================================================================
            const uint32_t vel_iters = std::max(1u, m_impl->config.num_velocity_iters);
            for (uint32_t iter = 0; iter < vel_iters; ++iter) {
                // Accumulate contact velocity deltas.
                {
                    auto *binding = &m_impl->accum_vel_stage->AllocateResourceBinding();
                    auto &srb = binding->GetShaderResourceBinding();
                    srb.BindBuffer("CollisionIds", *cr.collision_ids);
                    srb.BindBuffer("CollisionNormals", *cr.collision_normals);
                    srb.BindBuffer("ContactPointA", *cr.contact_point_a);
                    srb.BindBuffer("ContactPointB", *cr.contact_point_b);
                    srb.BindBuffer("CollisionCount", *cr.collision_count);
                    srb.BindBuffer("ShapeBoundRigidBody", *gpu.shape_bound_rigid_body);
                    srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                    srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
                    srb.BindBuffer("RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity);
                    srb.BindBuffer("RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity);
                    srb.BindBuffer("RigidBodyMass", *gpu.rigid_body_mass);
                    srb.BindBuffer("RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia);
                    srb.BindBuffer("RigidBodyDynamicFriction", *gpu.rigid_body_dynamic_friction);
                    srb.BindBuffer("RigidBodyRestitution", *gpu.rigid_body_restitution);
                    srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
                    srb.BindBuffer("PreContactLinearVelocity", *m_impl->gpu_pre_contact_linear_vel);
                    srb.BindBuffer("PreContactAngularVelocity", *m_impl->gpu_pre_contact_angular_vel);
                    srb.BindBuffer("SubstepStartPosition", *m_impl->gpu_substep_start_position);
                    srb.BindBuffer("SubstepStartOrientation", *m_impl->gpu_substep_start_orientation);
                    srb.BindBuffer("LinearVelocityDeltaI", *m_impl->gpu_linear_velocity_delta);
                    srb.BindBuffer("AngularVelocityDeltaI", *m_impl->gpu_angular_velocity_delta);
                    srb.BindBuffer("VelocityDeltaCount", *m_impl->gpu_velocity_delta_count);
                    srb.BindBuffer("ContactLagrange", *m_impl->gpu_contact_lagrange);
                    srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);

                    auto *stage = m_impl->accum_vel_stage.get();
                    builder.AddPass(
                        RenderGraphPassBuilder{m_impl->render_system}
                            .SetName("XPBD Accum Contact Vel")
                            .SetAffinity(RenderGraphPassAffinity::Compute)
                            .UseBuffer(coll_ids_h, RR)
                            .UseBuffer(coll_normals_h, RR)
                            .UseBuffer(coll_pta_h, RR)
                            .UseBuffer(coll_ptb_h, RR)
                            .UseBuffer(coll_cnt_h, RR)
                            .UseBuffer(shape2body_h, RR)
                            .UseBuffer(rot_h, RR)
                            .UseBuffer(linvel_h, RR)
                            .UseBuffer(angvel_h, RR)
                            .UseBuffer(alive_h, RR)
                            .UseBuffer(kinematic_h, RR)
                            .UseBuffer(mass_h, RR)
                            .UseBuffer(inv_inertia_h, RR)
                            .UseBuffer(dynfric_h, RR)
                            .UseBuffer(restitution_h, RR)
                            .UseBuffer(precont_lv_h, RR)
                            .UseBuffer(precont_av_h, RR)
                            .UseBuffer(ssp_pos_h, RR)
                            .UseBuffer(ssp_ori_h, RR)
                            .UseBuffer(linveldelta_h, RW)
                            .UseBuffer(angveldelta_h, RW)
                            .UseBuffer(velcntdelta_h, RW)
                            .UseBuffer(lagrange_h, RR)
                            .UseBuffer(uniforms_h, RR)
                            .SetPassFunction(
                                [stage, binding, contact_wg, pscene](CommandBuffer &cb, const RenderGraph &) -> void {
                                    if (!pscene->IsSimulationEnabled()) return;
                                    cb.BindComputeStage(*stage);
                                    cb.BindComputeResource(*binding);
                                    cb.DispatchCompute(contact_wg, 1, 1);
                                }
                            )
                            .Get()
                    );
                }

                // Apply body velocity deltas.
                {
                    auto *binding = &m_impl->apply_vel_stage->AllocateResourceBinding();
                    auto &srb = binding->GetShaderResourceBinding();
                    srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                    srb.BindBuffer("RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity);
                    srb.BindBuffer("RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity);
                    srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
                    srb.BindBuffer("LinearVelocityDeltaI", *m_impl->gpu_linear_velocity_delta);
                    srb.BindBuffer("AngularVelocityDeltaI", *m_impl->gpu_angular_velocity_delta);
                    srb.BindBuffer("VelocityDeltaCount", *m_impl->gpu_velocity_delta_count);

                    auto *stage = m_impl->apply_vel_stage.get();
                    builder.AddPass(
                        RenderGraphPassBuilder{m_impl->render_system}
                            .SetName("XPBD Apply Body Vel")
                            .SetAffinity(RenderGraphPassAffinity::Compute)
                            .UseBuffer(linvel_h, RW)
                            .UseBuffer(angvel_h, RW)
                            .UseBuffer(alive_h, RR)
                            .UseBuffer(kinematic_h, RR)
                            .UseBuffer(linveldelta_h, RW)
                            .UseBuffer(angveldelta_h, RW)
                            .UseBuffer(velcntdelta_h, RW)
                            .SetPassFunction(
                                [stage, binding, body_wg, pscene](CommandBuffer &cb, const RenderGraph &) -> void {
                                    if (!pscene->IsSimulationEnabled()) return;
                                    cb.BindComputeStage(*stage);
                                    cb.BindComputeResource(*binding);
                                    cb.DispatchCompute(body_wg, 1, 1);
                                }
                            )
                            .Get()
                    );
                }
            } // velocity iterations
        } // substep loop

        // ---- Model matrix update (after physics, always runs even when paused) ----
        if (gpu.model_matrices != nullptr && gpu.rigid_body_center_world_rotation != nullptr) {
            auto *binding = &m_impl->model_matrix_stage->AllocateResourceBinding();
            auto &mm_srb = binding->GetShaderResourceBinding();
            mm_srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
            mm_srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
            mm_srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
            mm_srb.BindBuffer("ModelMatrices", *gpu.model_matrices);

            auto model_matrices_handle =
                external_model_matrices_handle != RGBufferHandle{}
                    ? external_model_matrices_handle
                    : builder.ImportExternalResource(*gpu.model_matrices, {MemoryAccessTypeBufferBits::None});

            auto *mm_stage = m_impl->model_matrix_stage.get();

            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Model Matrix Update")
                    .UseBuffer(alive_h, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .UseBuffer(pos_h, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .UseBuffer(rot_h, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .UseBuffer(model_matrices_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .SetPassFunction([mm_stage, binding, body_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*mm_stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(body_wg, 1, 1);
                    })
                    .Get()
            );
        }
    }

} // namespace Engine
