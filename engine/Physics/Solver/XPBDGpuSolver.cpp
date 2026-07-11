#include "XpbdGpuSolver.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/Collision/ConvexCollisionDetector.h>
#include <Physics/Collision/SpatialHashBroadDetector.h>
#include <Physics/PhysicsScene.h>
#include <Render/Memory/ComputeBuffer.h>
#include <Render/Memory/DeviceBuffer.h>
#include <Render/Memory/MemoryAccessTypes.h>
#include <Render/Memory/ShaderParameters/ShaderResourceBinding.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/Compute/ComputeResourceBinding.h>
#include <Render/Pipeline/Compute/ComputeStage.h>
#include <Render/Pipeline/RenderGraph/RGAttachmentDesc.h>
#include <Render/Pipeline/RenderGraph/RenderGraph.h>
#include <Render/Pipeline/RenderGraph/RenderGraphBuilder.h>
#include <Render/Pipeline/RenderGraph/RenderGraphPass.h>
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
} // namespace

namespace Engine {

    // GpuStateSnapshot: cached parameters that trigger RG rebuild when they change.
    struct GpuStateSnapshot {
        uint32_t body_count = 0;
        uint32_t max_contacts = 0;
        uint32_t hinge_joint_count = 0;
        uint32_t fixed_joint_count = 0;
        uint32_t shape_count = 0;

        bool operator==(const GpuStateSnapshot &o) const {
            return body_count == o.body_count && max_contacts == o.max_contacts
                   && hinge_joint_count == o.hinge_joint_count && fixed_joint_count == o.fixed_joint_count
                   && shape_count == o.shape_count;
        }
        bool operator!=(const GpuStateSnapshot &o) const {
            return !(*this == o);
        }
    };

    struct XpbdGpuSolver::Impl {
        RenderSystem &render_system;

        bool shaders_loaded = false;
        XpbdConfig config{};

        // Cached snapshot for RG rebuild detection.
        GpuStateSnapshot cached_snapshot{};

        // ---- Owned collision detectors ----
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

        // ---- Self-owned RenderGraphs ----
        std::unique_ptr<RenderGraph> precollision_rg{};
        std::unique_ptr<RenderGraph> postcollision_preiter_rg{};
        std::unique_ptr<RenderGraph> position_iter_rg{};
        std::unique_ptr<RenderGraph> postposition_rg{};
        std::unique_ptr<RenderGraph> velocity_iter_rg{};
        std::unique_ptr<RenderGraph> model_matrix_rg{};

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
            snapshot_stage = load("solver/XPBDSolver/snapshot_position.comp.spv", "XPBD Snapshot");
            update_shape_world_pose_stage =
                load("solver/XPBDSolver/update_shape_world_pose.comp.spv", "XPBD UpdateShape");
            integrate_stage = load("solver/XPBDSolver/integrate_forces.comp.spv", "XPBD Integrate");
            accum_pos_stage = load("solver/XPBDSolver/accumulate_contact_position.comp.spv", "XPBD AccumPos");
            apply_pos_stage = load("solver/XPBDSolver/apply_body_position_deltas.comp.spv", "XPBD ApplyPos");
            update_vel_stage = load("solver/XPBDSolver/update_velocities_from_pose.comp.spv", "XPBD UpdateVel");
            accum_vel_stage = load("solver/XPBDSolver/accumulate_contact_velocity.comp.spv", "XPBD AccumVel");
            apply_vel_stage = load("solver/XPBDSolver/apply_body_velocity_deltas.comp.spv", "XPBD ApplyVel");
            model_matrix_stage = load("solver/XPBDSolver/model_matrix.comp.spv", "XPBD ModelMatrix");
            clear_hinge_lagrange_stage = load("solver/XPBDSolver/clear_hinge_lagrange.comp.spv", "XPBD ClearHinge");
            clear_fixed_lagrange_stage = load("solver/XPBDSolver/clear_fixed_lagrange.comp.spv", "XPBD ClearFixed");
            accum_hinge_pos_stage = load("solver/XPBDSolver/accumulate_hinge_position.comp.spv", "XPBD AccumHingePos");
            accum_fixed_pos_stage = load("solver/XPBDSolver/accumulate_fixed_position.comp.spv", "XPBD AccumFixedPos");
        }

        // Convenience aliases.
        using AT = MemoryAccessTypeBufferBits;
        static constexpr MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
        static constexpr MemoryAccessTypeBuffer RW{AT::ShaderRandomRead, AT::ShaderRandomWrite};
        static constexpr MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};
        static constexpr MemoryAccessTypeBuffer None{AT::None};
    };

    // =======================================================================
    // Constructor / Destructor
    // =======================================================================

    XpbdGpuSolver::XpbdGpuSolver(RenderSystem &render_system) : m_impl(std::make_unique<Impl>(render_system)) {
    }

    XpbdGpuSolver::~XpbdGpuSolver() = default;

    // =======================================================================
    // ISolver Interface
    // =======================================================================

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

        // Lazy shader loading.
        m_impl->EnsureShadersLoaded();

        const uint32_t body_count = gpu.rigid_body_slot_count;
        const uint32_t shape_count = gpu.shape_slot_count;
        const uint32_t all_pairs = shape_count > 1u ? (shape_count * (shape_count - 1u)) / 2u : 0u;
        const uint32_t max_contacts = std::max(1u, std::min(all_pairs * 5u, m_impl->config.max_contact_points));

        // Ensure intermediate buffers are sized.
        if (body_count != m_impl->cached_snapshot.body_count || max_contacts != m_impl->cached_snapshot.max_contacts
            || gpu.hinge_joint_count != m_impl->cached_snapshot.hinge_joint_count
            || gpu.fixed_joint_count != m_impl->cached_snapshot.fixed_joint_count) {
            m_impl->EnsureIntermediateBuffers(body_count, max_contacts, gpu.hinge_joint_count, gpu.fixed_joint_count);
        }

        // Write uniforms.
        const float substep_dt =
            m_impl->config.time_step / static_cast<float>(std::max(1u, m_impl->config.num_substep_perstep));
        {
            auto *uniform_addr = reinterpret_cast<glm::vec4 *>(m_impl->gpu_uniforms->GetVMAddress());
            *uniform_addr =
                glm::vec4(m_impl->config.gravity.x, m_impl->config.gravity.y, m_impl->config.gravity.z, substep_dt);
        }

        // Configure collision detectors.
        {
            // 1. Broad-phase: Configure creates internal pair buffers.
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

            // 2. Narrow-phase: Configure needs pair buffer pointers from broad-phase.
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

        // Update cached snapshot AFTER all allocations so RG rebuild sees the new counts.
        m_impl->cached_snapshot = {body_count, max_contacts, gpu.hinge_joint_count, gpu.fixed_joint_count, shape_count};
    }

    void XpbdGpuSolver::GPUStep(vk::CommandBuffer cb) {
        const auto gpu = m_bound_scene->GetGpuBuffers();
        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) return;

        const uint32_t body_count = gpu.rigid_body_slot_count;
        const uint32_t shape_count = gpu.shape_slot_count;
        const uint32_t all_pairs = shape_count > 1u ? (shape_count * (shape_count - 1u)) / 2u : 0u;
        const uint32_t max_contacts = std::max(1u, std::min(all_pairs * 5u, m_impl->config.max_contact_points));

        GpuStateSnapshot current{body_count, max_contacts, gpu.hinge_joint_count, gpu.fixed_joint_count, shape_count};
        bool snapshot_changed = (current != m_impl->cached_snapshot);

        // Rebuild RGs if snapshot changed.
        if (snapshot_changed || !m_impl->precollision_rg) {
            m_impl->precollision_rg = BuildPreCollisionRG();
        }
        if (snapshot_changed || !m_impl->postcollision_preiter_rg) {
            m_impl->postcollision_preiter_rg = BuildPostCollisionPreIterRG();
        }
        if (snapshot_changed || !m_impl->position_iter_rg) {
            m_impl->position_iter_rg = BuildPositionIterRG();
        }
        if (snapshot_changed || !m_impl->postposition_rg) {
            m_impl->postposition_rg = BuildPostPositionRG();
        }
        if (snapshot_changed || !m_impl->velocity_iter_rg) {
            m_impl->velocity_iter_rg = BuildVelocityIterRG();
        }
        if (snapshot_changed || !m_impl->model_matrix_rg) {
            m_impl->model_matrix_rg = BuildModelMatrixRG();
            m_impl->render_system.GetSceneDataManager().SetModelMatricesBuffer(gpu.model_matrices);
        }

        if (snapshot_changed) {
            m_impl->cached_snapshot = current;
        }

        if (m_bound_scene->IsSimulationEnabled()) {
            const uint32_t substep_count = std::max(1u, m_impl->config.num_substep_perstep);
            const uint32_t pos_iters = std::max(1u, m_impl->config.num_iter_persubstep);
            const uint32_t vel_iters = std::max(1u, m_impl->config.num_velocity_iters);

            // Substep loop.
            for (uint32_t ss = 0; ss < substep_count; ++ss) {
                // --- PreCollision RG ---
                if (m_impl->precollision_rg && m_impl->precollision_rg->GetNumPasses() > 0) {
                    m_impl->precollision_rg->RecordAllPasses(cb);
                }

                // --- Broad-phase Detect ---
                m_impl->broad_detector->Detect(cb);
                // --- Narrow-phase Detect ---
                m_impl->narrow_detector->Detect(cb);

                // --- PostCollision PreIter RG ---
                if (m_impl->postcollision_preiter_rg && m_impl->postcollision_preiter_rg->GetNumPasses() > 0) {
                    m_impl->postcollision_preiter_rg->RecordAllPasses(cb);
                }

                // --- Position iterations ---
                for (uint32_t iter = 0; iter < pos_iters; ++iter) {
                    if (m_impl->position_iter_rg && m_impl->position_iter_rg->GetNumPasses() > 0) {
                        m_impl->position_iter_rg->RecordAllPasses(cb);
                    }
                }

                // --- PostPosition RG ---
                if (m_impl->postposition_rg && m_impl->postposition_rg->GetNumPasses() > 0) {
                    m_impl->postposition_rg->RecordAllPasses(cb);
                }

                // --- Velocity iterations ---
                for (uint32_t iter = 0; iter < vel_iters; ++iter) {
                    if (m_impl->velocity_iter_rg && m_impl->velocity_iter_rg->GetNumPasses() > 0) {
                        m_impl->velocity_iter_rg->RecordAllPasses(cb);
                    }
                }
            }
        }

        // --- ModelMatrix RG (always, even when paused) ---
        if (m_impl->model_matrix_rg && m_impl->model_matrix_rg->GetNumPasses() > 0) {
            m_impl->model_matrix_rg->RecordAllPasses(cb);
        }
    }

    // =======================================================================
    // RG Build Functions
    // =======================================================================

    std::unique_ptr<RenderGraph> XpbdGpuSolver::BuildPreCollisionRG() {
        const auto gpu = m_bound_scene->GetGpuBuffers();
        const uint32_t body_count = gpu.rigid_body_slot_count;
        const uint32_t shape_count = gpu.shape_slot_count;
        const uint32_t body_wg = (body_count + 63u) / 64u;
        const uint32_t shape_wg = (shape_count + 63u) / 64u;

        RenderGraphBuilder builder{m_impl->render_system};

        // prev_access = {AT::None} — first RG in the substep chain.

        // Scene buffers.
        auto alive_h = builder.ImportExternalResource(*gpu.rigid_body_alive, Impl::RR);
        auto pos_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_position, Impl::RW);
        auto rot_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_rotation, Impl::RW);
        auto linvel_h = builder.ImportExternalResource(*gpu.rigid_body_linear_velocity, Impl::RW);
        auto angvel_h = builder.ImportExternalResource(*gpu.rigid_body_angular_velocity, Impl::RW);
        auto mass_h = builder.ImportExternalResource(*gpu.rigid_body_mass, Impl::RR);
        auto inv_inertia_h = builder.ImportExternalResource(*gpu.rigid_body_inverse_inertia, Impl::RR);
        auto inertia_h = builder.ImportExternalResource(*gpu.rigid_body_inertia, Impl::RR);
        auto extforce_h = builder.ImportExternalResource(*gpu.rigid_body_external_force, Impl::RR);
        auto exttorque_h = builder.ImportExternalResource(*gpu.rigid_body_external_torque, Impl::RR);
        auto kinematic_h = builder.ImportExternalResource(*gpu.rigid_body_is_kinematic, Impl::RR);

        // Shape world buffers (written by this RG).
        auto shape_alive_h = builder.ImportExternalResource(*gpu.shape_alive, Impl::RR);
        auto shape2body_h = builder.ImportExternalResource(*gpu.shape_bound_rigid_body, Impl::RR);
        auto shape_local_pos_h = builder.ImportExternalResource(*gpu.shape_local_position, Impl::RR);
        auto shape_local_rot_h = builder.ImportExternalResource(*gpu.shape_local_rotation, Impl::RR);
        auto shape_wpos_h = builder.ImportExternalResource(*gpu.shape_world_position, Impl::RW);
        auto shape_wrot_h = builder.ImportExternalResource(*gpu.shape_world_rotation, Impl::RW);

        // Internal buffers.
        auto uniforms_h = builder.ImportExternalResource(*m_impl->gpu_uniforms, Impl::RR);
        auto body_cnt_h = builder.ImportExternalResource(*m_impl->gpu_body_count_buffer, Impl::RR);
        auto precont_lv_h = builder.ImportExternalResource(*m_impl->gpu_pre_contact_linear_vel, Impl::RW);
        auto precont_av_h = builder.ImportExternalResource(*m_impl->gpu_pre_contact_angular_vel, Impl::RW);
        auto ssp_pos_h = builder.ImportExternalResource(*m_impl->gpu_substep_start_position, Impl::RW);
        auto ssp_ori_h = builder.ImportExternalResource(*m_impl->gpu_substep_start_orientation, Impl::RW);

        // Snapshot passes.
        auto AddSnap = [&](const ComputeBuffer &src,
                           const ComputeBuffer &dst,
                           RGBufferHandle src_h,
                           RGBufferHandle dst_h,
                           const char *name) {
            auto *binding = &m_impl->snapshot_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("SrcBuffer", src);
            srb.BindBuffer("DstBuffer", dst);
            srb.BindBuffer("ElemCount", *m_impl->gpu_body_count_buffer);
            auto *stage = m_impl->snapshot_stage.get();
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName(name)
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(src_h, Impl::RR)
                    .UseBuffer(dst_h, Impl::WW)
                    .UseBuffer(body_cnt_h, Impl::RR)
                    .SetPassFunction([stage, binding, body_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(body_wg, 1, 1);
                    })
                    .Get()
            );
        };
        AddSnap(
            *gpu.rigid_body_center_world_position,
            *m_impl->gpu_substep_start_position,
            pos_h,
            ssp_pos_h,
            "XPBD Snap SubstepStartPos"
        );
        AddSnap(
            *gpu.rigid_body_center_world_rotation,
            *m_impl->gpu_substep_start_orientation,
            rot_h,
            ssp_ori_h,
            "XPBD Snap SubstepStartOri"
        );

        // Integrate forces.
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
                    .UseBuffer(alive_h, Impl::RR)
                    .UseBuffer(pos_h, Impl::RW)
                    .UseBuffer(rot_h, Impl::RW)
                    .UseBuffer(linvel_h, Impl::RW)
                    .UseBuffer(angvel_h, Impl::RW)
                    .UseBuffer(mass_h, Impl::RR)
                    .UseBuffer(inv_inertia_h, Impl::RR)
                    .UseBuffer(inertia_h, Impl::RR)
                    .UseBuffer(extforce_h, Impl::RR)
                    .UseBuffer(exttorque_h, Impl::RR)
                    .UseBuffer(kinematic_h, Impl::RR)
                    .UseBuffer(uniforms_h, Impl::RR)
                    .SetPassFunction([stage, binding, body_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(body_wg, 1, 1);
                    })
                    .Get()
            );
        }

        // Pre-contact velocity snapshots (after force integration, for restitution reference).
        AddSnap(
            *gpu.rigid_body_linear_velocity,
            *m_impl->gpu_pre_contact_linear_vel,
            linvel_h,
            precont_lv_h,
            "XPBD Snap PreContactLinVel"
        );
        AddSnap(
            *gpu.rigid_body_angular_velocity,
            *m_impl->gpu_pre_contact_angular_vel,
            angvel_h,
            precont_av_h,
            "XPBD Snap PreContactAngVel"
        );

        // Update shape world poses.
        if (shape_count > 1u && gpu.shape_world_position != nullptr) {
            auto *binding = &m_impl->update_shape_world_pose_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
            srb.BindBuffer("ShapeBoundRigidBody", *gpu.shape_bound_rigid_body);
            srb.BindBuffer("ShapeLocalPosition", *gpu.shape_local_position);
            srb.BindBuffer("ShapeLocalRotation", *gpu.shape_local_rotation);
            srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
            srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
            srb.BindBuffer("ShapeWorldPosition", *gpu.shape_world_position);
            srb.BindBuffer("ShapeWorldRotation", *gpu.shape_world_rotation);
            auto *stage = m_impl->update_shape_world_pose_stage.get();
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Update Shape World Pose")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(shape_alive_h, Impl::RR)
                    .UseBuffer(shape2body_h, Impl::RR)
                    .UseBuffer(shape_local_pos_h, Impl::RR)
                    .UseBuffer(shape_local_rot_h, Impl::RR)
                    .UseBuffer(pos_h, Impl::RR)
                    .UseBuffer(rot_h, Impl::RR)
                    .UseBuffer(shape_wpos_h, Impl::WW)
                    .UseBuffer(shape_wrot_h, Impl::WW)
                    .SetPassFunction([stage, binding, shape_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(shape_wg, 1, 1);
                    })
                    .Get()
            );
        }

        return builder.BuildRenderGraph();
    }

    std::unique_ptr<RenderGraph> XpbdGpuSolver::BuildPostCollisionPreIterRG() {
        const auto gpu = m_bound_scene->GetGpuBuffers();
        const uint32_t contact_wg = (m_impl->cached_snapshot.max_contacts + 63u) / 64u;

        RenderGraphBuilder builder{m_impl->render_system};

        // Clear contact Lagrange.
        {
            auto lagrange_h = builder.ImportExternalResource(*m_impl->gpu_contact_lagrange, Impl::None);
            auto contact_cnt_h = builder.ImportExternalResource(*m_impl->gpu_contact_count_buffer, Impl::None);

            auto *binding = &m_impl->clear_int_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("Target", *m_impl->gpu_contact_lagrange);
            srb.BindBuffer("ElemCount", *m_impl->gpu_contact_count_buffer);
            auto *stage = m_impl->clear_int_stage.get();
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Memset Lagrange")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(lagrange_h, Impl::WW)
                    .UseBuffer(contact_cnt_h, Impl::RR)
                    .SetPassFunction([stage, binding, contact_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(contact_wg, 1, 1);
                    })
                    .Get()
            );
        }

        // Clear hinge Lagrange (conditional).
        if (gpu.hinge_joint_count > 0) {
            auto hinge_axis_h = builder.ImportExternalResource(*m_impl->gpu_hinge_axis_lagrange, Impl::None);
            auto hinge_anchor_h = builder.ImportExternalResource(*m_impl->gpu_hinge_anchor_lagrange, Impl::None);
            auto hinge_cnt_h = builder.ImportExternalResource(*m_impl->gpu_hinge_joint_count_buffer, Impl::None);

            auto *binding = &m_impl->clear_hinge_lagrange_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("HingeAxisLagrange", *m_impl->gpu_hinge_axis_lagrange);
            srb.BindBuffer("HingeAnchorLagrange", *m_impl->gpu_hinge_anchor_lagrange);
            srb.BindBuffer("HingeJointCount", *m_impl->gpu_hinge_joint_count_buffer);
            auto *stage = m_impl->clear_hinge_lagrange_stage.get();
            uint32_t hinge_wg = (gpu.hinge_joint_count + 255u) / 256u;
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Memset Hinge Lagrange")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(hinge_axis_h, Impl::WW)
                    .UseBuffer(hinge_anchor_h, Impl::WW)
                    .UseBuffer(hinge_cnt_h, Impl::RR)
                    .SetPassFunction([stage, binding, hinge_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(hinge_wg, 1, 1);
                    })
                    .Get()
            );
        }

        // Clear fixed Lagrange (conditional).
        if (gpu.fixed_joint_count > 0) {
            auto fixed_rot_h = builder.ImportExternalResource(*m_impl->gpu_fixed_rotation_lagrange, Impl::None);
            auto fixed_pos_h = builder.ImportExternalResource(*m_impl->gpu_fixed_position_lagrange, Impl::None);
            auto fixed_cnt_h = builder.ImportExternalResource(*m_impl->gpu_fixed_joint_count_buffer, Impl::None);

            auto *binding = &m_impl->clear_fixed_lagrange_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("FixedRotationLagrange", *m_impl->gpu_fixed_rotation_lagrange);
            srb.BindBuffer("FixedPositionLagrange", *m_impl->gpu_fixed_position_lagrange);
            srb.BindBuffer("FixedJointCount", *m_impl->gpu_fixed_joint_count_buffer);
            auto *stage = m_impl->clear_fixed_lagrange_stage.get();
            uint32_t fixed_wg = (gpu.fixed_joint_count + 255u) / 256u;
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Memset Fixed Lagrange")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(fixed_rot_h, Impl::WW)
                    .UseBuffer(fixed_pos_h, Impl::WW)
                    .UseBuffer(fixed_cnt_h, Impl::RR)
                    .SetPassFunction([stage, binding, fixed_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(fixed_wg, 1, 1);
                    })
                    .Get()
            );
        }

        return builder.BuildRenderGraph();
    }

    std::unique_ptr<RenderGraph> XpbdGpuSolver::BuildPositionIterRG() {
        // LOOP RG: all mutable buffers use prev_access = RW (conservative for re-recording).
        const auto gpu = m_bound_scene->GetGpuBuffers();
        const auto narrow = m_impl->narrow_detector->GetResultBuffers();
        const uint32_t body_wg = (m_impl->cached_snapshot.body_count + 63u) / 64u;
        const uint32_t contact_wg = (m_impl->cached_snapshot.max_contacts + 63u) / 64u;

        RenderGraphBuilder builder{m_impl->render_system};

        // Scene buffers.
        auto pos_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_position, Impl::RW);
        auto rot_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_rotation, Impl::RW);
        auto alive_h = builder.ImportExternalResource(*gpu.rigid_body_alive, Impl::RR);
        auto kinematic_h = builder.ImportExternalResource(*gpu.rigid_body_is_kinematic, Impl::RR);
        auto mass_h = builder.ImportExternalResource(*gpu.rigid_body_mass, Impl::RR);
        auto inv_inertia_h = builder.ImportExternalResource(*gpu.rigid_body_inverse_inertia, Impl::RR);
        auto shape2body_h = builder.ImportExternalResource(*gpu.shape_bound_rigid_body, Impl::RR);

        // Internal buffers.
        auto lindelta_h = builder.ImportExternalResource(*m_impl->gpu_linear_position_delta, Impl::RW);
        auto angdelta_h = builder.ImportExternalResource(*m_impl->gpu_angular_position_delta, Impl::RW);
        auto cntdelta_h = builder.ImportExternalResource(*m_impl->gpu_position_delta_count, Impl::RW);
        auto lagrange_h = builder.ImportExternalResource(*m_impl->gpu_contact_lagrange, Impl::RW);
        auto ssp_pos_h = builder.ImportExternalResource(*m_impl->gpu_substep_start_position, Impl::RR);
        auto ssp_ori_h = builder.ImportExternalResource(*m_impl->gpu_substep_start_orientation, Impl::RR);
        auto shape_local_pos_h = builder.ImportExternalResource(*gpu.shape_local_position, Impl::RR);
        auto shape_local_rot_h = builder.ImportExternalResource(*gpu.shape_local_rotation, Impl::RR);
        auto uniforms_h = builder.ImportExternalResource(*m_impl->gpu_uniforms, Impl::RR);

        // Narrow-phase collision results
        auto coll_ids_h = builder.ImportExternalResource(*narrow.collision_ids, Impl::RW);
        auto coll_normals_h = builder.ImportExternalResource(*narrow.collision_normals, Impl::RW);
        auto coll_pta_h = builder.ImportExternalResource(*narrow.contact_point_a, Impl::RW);
        auto coll_ptb_h = builder.ImportExternalResource(*narrow.contact_point_b, Impl::RW);
        auto coll_cnt_h = builder.ImportExternalResource(*narrow.collision_count, Impl::RW);

        // Accumulate contact position deltas.
        {
            auto *binding = &m_impl->accum_pos_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("CollisionIds", *narrow.collision_ids);
            srb.BindBuffer("CollisionNormals", *narrow.collision_normals);
            srb.BindBuffer("ContactPointA", *narrow.contact_point_a);
            srb.BindBuffer("ContactPointB", *narrow.contact_point_b);
            srb.BindBuffer("CollisionCount", *narrow.collision_count);
            srb.BindBuffer("ShapeBoundRigidBody", *gpu.shape_bound_rigid_body);
            srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
            srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
            srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
            srb.BindBuffer("RigidBodyMass", *gpu.rigid_body_mass);
            srb.BindBuffer("RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia);
            srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
            srb.BindBuffer("ShapeLocalPosition", *gpu.shape_local_position);
            srb.BindBuffer("ShapeLocalRotation", *gpu.shape_local_rotation);
            srb.BindBuffer("LinearPositionDelta", *m_impl->gpu_linear_position_delta);
            srb.BindBuffer("AngularPositionDelta", *m_impl->gpu_angular_position_delta);
            srb.BindBuffer("PositionDeltaCount", *m_impl->gpu_position_delta_count);
            srb.BindBuffer("ContactLagrange", *m_impl->gpu_contact_lagrange);
            auto *stage = m_impl->accum_pos_stage.get();
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Accum Contact Pos")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(coll_ids_h, Impl::RR)
                    .UseBuffer(coll_normals_h, Impl::RR)
                    .UseBuffer(coll_pta_h, Impl::RR)
                    .UseBuffer(coll_ptb_h, Impl::RR)
                    .UseBuffer(coll_cnt_h, Impl::RR)
                    .UseBuffer(shape2body_h, Impl::RR)
                    .UseBuffer(pos_h, Impl::RR)
                    .UseBuffer(rot_h, Impl::RR)
                    .UseBuffer(alive_h, Impl::RR)
                    .UseBuffer(kinematic_h, Impl::RR)
                    .UseBuffer(mass_h, Impl::RR)
                    .UseBuffer(inv_inertia_h, Impl::RR)
                    .UseBuffer(shape_local_pos_h, Impl::RR)
                    .UseBuffer(shape_local_rot_h, Impl::RR)
                    .UseBuffer(lindelta_h, Impl::RW)
                    .UseBuffer(angdelta_h, Impl::RW)
                    .UseBuffer(cntdelta_h, Impl::RW)
                    .UseBuffer(lagrange_h, Impl::RW)
                    .SetPassFunction([stage, binding, contact_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(contact_wg, 1, 1);
                    })
                    .Get()
            );
        }

        // Accumulate hinge position deltas (conditional on hinge joint count).
        if (gpu.hinge_joint_count > 0 && gpu.gpu_hinge_joints != nullptr) {
            auto hinge_joints_h = builder.ImportExternalResource(*gpu.gpu_hinge_joints, Impl::RR);
            auto hinge_cnt_h = builder.ImportExternalResource(*m_impl->gpu_hinge_joint_count_buffer, Impl::RR);
            auto hinge_axis_h = builder.ImportExternalResource(*m_impl->gpu_hinge_axis_lagrange, Impl::RW);
            auto hinge_anchor_h = builder.ImportExternalResource(*m_impl->gpu_hinge_anchor_lagrange, Impl::RW);

            auto *binding = &m_impl->accum_hinge_pos_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("HingeJoints", *gpu.gpu_hinge_joints);
            srb.BindBuffer("HingeJointCount", *m_impl->gpu_hinge_joint_count_buffer);
            srb.BindBuffer("HingeAxisLagrange", *m_impl->gpu_hinge_axis_lagrange);
            srb.BindBuffer("HingeAnchorLagrange", *m_impl->gpu_hinge_anchor_lagrange);
            srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
            srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
            srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
            srb.BindBuffer("RigidBodyMass", *gpu.rigid_body_mass);
            srb.BindBuffer("RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia);
            srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
            srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);
            srb.BindBuffer("LinearPositionDelta", *m_impl->gpu_linear_position_delta);
            srb.BindBuffer("AngularPositionDelta", *m_impl->gpu_angular_position_delta);
            srb.BindBuffer("PositionDeltaCount", *m_impl->gpu_position_delta_count);
            auto *stage = m_impl->accum_hinge_pos_stage.get();
            uint32_t hinge_wg = (gpu.hinge_joint_count + 63u) / 64u;
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Accum Hinge Pos")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(hinge_joints_h, Impl::RR)
                    .UseBuffer(hinge_cnt_h, Impl::RR)
                    .UseBuffer(hinge_axis_h, Impl::RW)
                    .UseBuffer(hinge_anchor_h, Impl::RW)
                    .UseBuffer(pos_h, Impl::RR)
                    .UseBuffer(rot_h, Impl::RR)
                    .UseBuffer(alive_h, Impl::RR)
                    .UseBuffer(kinematic_h, Impl::RR)
                    .UseBuffer(mass_h, Impl::RR)
                    .UseBuffer(inv_inertia_h, Impl::RR)
                    .UseBuffer(uniforms_h, Impl::RR)
                    .UseBuffer(lindelta_h, Impl::RW)
                    .UseBuffer(angdelta_h, Impl::RW)
                    .UseBuffer(cntdelta_h, Impl::RW)
                    .SetPassFunction([stage, binding, hinge_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(hinge_wg, 1, 1);
                    })
                    .Get()
            );
        }

        // Accumulate fixed position deltas (conditional on fixed joint count).
        if (gpu.fixed_joint_count > 0 && gpu.gpu_fixed_joints != nullptr) {
            auto fixed_joints_h = builder.ImportExternalResource(*gpu.gpu_fixed_joints, Impl::RR);
            auto fixed_cnt_h = builder.ImportExternalResource(*m_impl->gpu_fixed_joint_count_buffer, Impl::RR);
            auto fixed_rot_h = builder.ImportExternalResource(*m_impl->gpu_fixed_rotation_lagrange, Impl::RW);
            auto fixed_pos_lag_h = builder.ImportExternalResource(*m_impl->gpu_fixed_position_lagrange, Impl::RW);

            auto *binding = &m_impl->accum_fixed_pos_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("FixedJoints", *gpu.gpu_fixed_joints);
            srb.BindBuffer("FixedJointCount", *m_impl->gpu_fixed_joint_count_buffer);
            srb.BindBuffer("FixedRotationLagrange", *m_impl->gpu_fixed_rotation_lagrange);
            srb.BindBuffer("FixedPositionLagrange", *m_impl->gpu_fixed_position_lagrange);
            srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
            srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
            srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
            srb.BindBuffer("RigidBodyMass", *gpu.rigid_body_mass);
            srb.BindBuffer("RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia);
            srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
            srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);
            srb.BindBuffer("LinearPositionDelta", *m_impl->gpu_linear_position_delta);
            srb.BindBuffer("AngularPositionDelta", *m_impl->gpu_angular_position_delta);
            srb.BindBuffer("PositionDeltaCount", *m_impl->gpu_position_delta_count);
            auto *stage = m_impl->accum_fixed_pos_stage.get();
            uint32_t fixed_wg = (gpu.fixed_joint_count + 63u) / 64u;
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Accum Fixed Pos")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(fixed_joints_h, Impl::RR)
                    .UseBuffer(fixed_cnt_h, Impl::RR)
                    .UseBuffer(fixed_rot_h, Impl::RW)
                    .UseBuffer(fixed_pos_lag_h, Impl::RW)
                    .UseBuffer(pos_h, Impl::RR)
                    .UseBuffer(rot_h, Impl::RR)
                    .UseBuffer(alive_h, Impl::RR)
                    .UseBuffer(kinematic_h, Impl::RR)
                    .UseBuffer(mass_h, Impl::RR)
                    .UseBuffer(inv_inertia_h, Impl::RR)
                    .UseBuffer(uniforms_h, Impl::RR)
                    .UseBuffer(lindelta_h, Impl::RW)
                    .UseBuffer(angdelta_h, Impl::RW)
                    .UseBuffer(cntdelta_h, Impl::RW)
                    .SetPassFunction([stage, binding, fixed_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(fixed_wg, 1, 1);
                    })
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
            srb.BindBuffer("LinearPositionDelta", *m_impl->gpu_linear_position_delta);
            srb.BindBuffer("AngularPositionDelta", *m_impl->gpu_angular_position_delta);
            srb.BindBuffer("PositionDeltaCount", *m_impl->gpu_position_delta_count);
            auto *stage = m_impl->apply_pos_stage.get();
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Apply Body Pos")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(pos_h, Impl::RW)
                    .UseBuffer(rot_h, Impl::RW)
                    .UseBuffer(alive_h, Impl::RR)
                    .UseBuffer(kinematic_h, Impl::RR)
                    .UseBuffer(lindelta_h, Impl::RW)
                    .UseBuffer(angdelta_h, Impl::RW)
                    .UseBuffer(cntdelta_h, Impl::RW)
                    .SetPassFunction([stage, binding, body_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(body_wg, 1, 1);
                    })
                    .Get()
            );
        }

        return builder.BuildRenderGraph();
    }

    std::unique_ptr<RenderGraph> XpbdGpuSolver::BuildPostPositionRG() {
        const auto gpu = m_bound_scene->GetGpuBuffers();
        const uint32_t body_wg = (m_impl->cached_snapshot.body_count + 63u) / 64u;

        RenderGraphBuilder builder{m_impl->render_system};

        auto pos_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_position, Impl::RW);
        auto rot_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_rotation, Impl::RW);
        auto linvel_h = builder.ImportExternalResource(*gpu.rigid_body_linear_velocity, Impl::RW);
        auto angvel_h = builder.ImportExternalResource(*gpu.rigid_body_angular_velocity, Impl::RW);
        auto alive_h = builder.ImportExternalResource(*gpu.rigid_body_alive, Impl::RR);
        auto kinematic_h = builder.ImportExternalResource(*gpu.rigid_body_is_kinematic, Impl::RR);
        auto ssp_pos_h = builder.ImportExternalResource(*m_impl->gpu_substep_start_position, Impl::RW);
        auto ssp_ori_h = builder.ImportExternalResource(*m_impl->gpu_substep_start_orientation, Impl::RW);
        auto uniforms_h = builder.ImportExternalResource(*m_impl->gpu_uniforms, Impl::RR);

        {
            auto *binding = &m_impl->update_vel_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
            srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
            srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
            srb.BindBuffer("RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity);
            srb.BindBuffer("RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity);
            srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
            srb.BindBuffer("SubstepStartPosition", *m_impl->gpu_substep_start_position);
            srb.BindBuffer("SubstepStartOrientation", *m_impl->gpu_substep_start_orientation);
            srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);
            auto *stage = m_impl->update_vel_stage.get();
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Update Velocities")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(linvel_h, Impl::RW)
                    .UseBuffer(angvel_h, Impl::RW)
                    .UseBuffer(pos_h, Impl::RR)
                    .UseBuffer(rot_h, Impl::RR)
                    .UseBuffer(alive_h, Impl::RR)
                    .UseBuffer(kinematic_h, Impl::RR)
                    .UseBuffer(ssp_pos_h, Impl::RR)
                    .UseBuffer(ssp_ori_h, Impl::RR)
                    .UseBuffer(uniforms_h, Impl::RR)
                    .SetPassFunction([stage, binding, body_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(body_wg, 1, 1);
                    })
                    .Get()
            );
        }

        return builder.BuildRenderGraph();
    }

    std::unique_ptr<RenderGraph> XpbdGpuSolver::BuildVelocityIterRG() {
        // LOOP RG: conservative prev_access = RW for mutable buffers.
        const auto gpu = m_bound_scene->GetGpuBuffers();
        const auto narrow = m_impl->narrow_detector->GetResultBuffers();
        const uint32_t body_wg = (m_impl->cached_snapshot.body_count + 63u) / 64u;
        const uint32_t contact_wg = (m_impl->cached_snapshot.max_contacts + 63u) / 64u;

        RenderGraphBuilder builder{m_impl->render_system};

        auto linvel_h = builder.ImportExternalResource(*gpu.rigid_body_linear_velocity, Impl::RW);
        auto angvel_h = builder.ImportExternalResource(*gpu.rigid_body_angular_velocity, Impl::RW);
        auto rot_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_rotation, Impl::RR);
        auto alive_h = builder.ImportExternalResource(*gpu.rigid_body_alive, Impl::RR);
        auto kinematic_h = builder.ImportExternalResource(*gpu.rigid_body_is_kinematic, Impl::RR);
        auto mass_h = builder.ImportExternalResource(*gpu.rigid_body_mass, Impl::RR);
        auto inv_inertia_h = builder.ImportExternalResource(*gpu.rigid_body_inverse_inertia, Impl::RR);
        auto dynfric_h = builder.ImportExternalResource(*gpu.rigid_body_dynamic_friction, Impl::RR);
        auto restitution_h = builder.ImportExternalResource(*gpu.rigid_body_restitution, Impl::RR);
        auto shape2body_h = builder.ImportExternalResource(*gpu.shape_bound_rigid_body, Impl::RR);

        auto precont_lv_h = builder.ImportExternalResource(*m_impl->gpu_pre_contact_linear_vel, Impl::RR);
        auto precont_av_h = builder.ImportExternalResource(*m_impl->gpu_pre_contact_angular_vel, Impl::RR);
        auto ssp_pos_h = builder.ImportExternalResource(*m_impl->gpu_substep_start_position, Impl::RR);
        auto ssp_ori_h = builder.ImportExternalResource(*m_impl->gpu_substep_start_orientation, Impl::RR);
        auto shape_local_pos_h2 = builder.ImportExternalResource(*gpu.shape_local_position, Impl::RR);
        auto shape_local_rot_h2 = builder.ImportExternalResource(*gpu.shape_local_rotation, Impl::RR);
        auto linveldelta_h = builder.ImportExternalResource(*m_impl->gpu_linear_velocity_delta, Impl::RW);
        auto angveldelta_h = builder.ImportExternalResource(*m_impl->gpu_angular_velocity_delta, Impl::RW);
        auto velcntdelta_h = builder.ImportExternalResource(*m_impl->gpu_velocity_delta_count, Impl::RW);
        auto lagrange_h = builder.ImportExternalResource(*m_impl->gpu_contact_lagrange, Impl::RW);
        auto uniforms_h = builder.ImportExternalResource(*m_impl->gpu_uniforms, Impl::RR);

        // Narrow-phase collision results
        auto coll_ids_h = builder.ImportExternalResource(*narrow.collision_ids, Impl::RW);
        auto coll_normals_h = builder.ImportExternalResource(*narrow.collision_normals, Impl::RW);
        auto coll_pta_h = builder.ImportExternalResource(*narrow.contact_point_a, Impl::RW);
        auto coll_ptb_h = builder.ImportExternalResource(*narrow.contact_point_b, Impl::RW);
        auto coll_cnt_h = builder.ImportExternalResource(*narrow.collision_count, Impl::RW);

        // Accumulate contact velocity deltas.
        {
            auto *binding = &m_impl->accum_vel_stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            srb.BindBuffer("CollisionIds", *narrow.collision_ids);
            srb.BindBuffer("CollisionNormals", *narrow.collision_normals);
            srb.BindBuffer("ContactPointA", *narrow.contact_point_a);
            srb.BindBuffer("ContactPointB", *narrow.contact_point_b);
            srb.BindBuffer("CollisionCount", *narrow.collision_count);
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
            srb.BindBuffer("ShapeLocalPosition", *gpu.shape_local_position);
            srb.BindBuffer("ShapeLocalRotation", *gpu.shape_local_rotation);
            srb.BindBuffer("LinearVelocityDelta", *m_impl->gpu_linear_velocity_delta);
            srb.BindBuffer("AngularVelocityDelta", *m_impl->gpu_angular_velocity_delta);
            srb.BindBuffer("VelocityDeltaCount", *m_impl->gpu_velocity_delta_count);
            srb.BindBuffer("ContactLagrange", *m_impl->gpu_contact_lagrange);
            srb.BindBuffer("XpbdUniforms", *m_impl->gpu_uniforms);
            auto *stage = m_impl->accum_vel_stage.get();
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Accum Contact Vel")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(coll_ids_h, Impl::RR)
                    .UseBuffer(coll_normals_h, Impl::RR)
                    .UseBuffer(coll_pta_h, Impl::RR)
                    .UseBuffer(coll_ptb_h, Impl::RR)
                    .UseBuffer(coll_cnt_h, Impl::RR)
                    .UseBuffer(shape2body_h, Impl::RR)
                    .UseBuffer(rot_h, Impl::RR)
                    .UseBuffer(linvel_h, Impl::RR)
                    .UseBuffer(angvel_h, Impl::RR)
                    .UseBuffer(alive_h, Impl::RR)
                    .UseBuffer(kinematic_h, Impl::RR)
                    .UseBuffer(mass_h, Impl::RR)
                    .UseBuffer(inv_inertia_h, Impl::RR)
                    .UseBuffer(dynfric_h, Impl::RR)
                    .UseBuffer(restitution_h, Impl::RR)
                    .UseBuffer(precont_lv_h, Impl::RR)
                    .UseBuffer(precont_av_h, Impl::RR)
                    .UseBuffer(shape_local_pos_h2, Impl::RR)
                    .UseBuffer(shape_local_rot_h2, Impl::RR)
                    .UseBuffer(linveldelta_h, Impl::RW)
                    .UseBuffer(angveldelta_h, Impl::RW)
                    .UseBuffer(velcntdelta_h, Impl::RW)
                    .UseBuffer(lagrange_h, Impl::RR)
                    .UseBuffer(uniforms_h, Impl::RR)
                    .SetPassFunction([stage, binding, contact_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(contact_wg, 1, 1);
                    })
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
            srb.BindBuffer("LinearVelocityDelta", *m_impl->gpu_linear_velocity_delta);
            srb.BindBuffer("AngularVelocityDelta", *m_impl->gpu_angular_velocity_delta);
            srb.BindBuffer("VelocityDeltaCount", *m_impl->gpu_velocity_delta_count);
            auto *stage = m_impl->apply_vel_stage.get();
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("XPBD Apply Body Vel")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(linvel_h, Impl::RW)
                    .UseBuffer(angvel_h, Impl::RW)
                    .UseBuffer(alive_h, Impl::RR)
                    .UseBuffer(kinematic_h, Impl::RR)
                    .UseBuffer(linveldelta_h, Impl::RW)
                    .UseBuffer(angveldelta_h, Impl::RW)
                    .UseBuffer(velcntdelta_h, Impl::RW)
                    .SetPassFunction([stage, binding, body_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(body_wg, 1, 1);
                    })
                    .Get()
            );
        }

        return builder.BuildRenderGraph();
    }

    std::unique_ptr<RenderGraph> XpbdGpuSolver::BuildModelMatrixRG() {
        const auto gpu = m_bound_scene->GetGpuBuffers();
        const uint32_t body_wg = (m_impl->cached_snapshot.body_count + 63u) / 64u;

        RenderGraphBuilder builder{m_impl->render_system};

        auto alive_h = builder.ImportExternalResource(*gpu.rigid_body_alive, Impl::RR);
        auto pos_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_position, Impl::RR);
        auto rot_h = builder.ImportExternalResource(*gpu.rigid_body_center_world_rotation, Impl::RR);
        auto off_h = builder.ImportExternalResource(*gpu.rigid_body_center_offset_local_position, Impl::RR);
        auto mm_h = builder.ImportExternalResource(*gpu.model_matrices, Impl::None);

        auto *stage = m_impl->model_matrix_stage.get();
        auto *binding = &m_impl->model_matrix_stage->AllocateResourceBinding();
        auto &srb = binding->GetShaderResourceBinding();
        srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
        srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);
        srb.BindBuffer("RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation);
        srb.BindBuffer("RigidBodyCenterOffsetLocal", *gpu.rigid_body_center_offset_local_position);
        srb.BindBuffer("ModelMatrices", *gpu.model_matrices);

        builder.AddPass(
            RenderGraphPassBuilder{m_impl->render_system}
                .SetName("XPBD Model Matrix")
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .UseBuffer(alive_h, Impl::RR)
                .UseBuffer(pos_h, Impl::RR)
                .UseBuffer(rot_h, Impl::RR)
                .UseBuffer(off_h, Impl::RR)
                .UseBuffer(mm_h, Impl::WW)
                .SetPassFunction([stage, binding, body_wg](CommandBuffer &cb, const RenderGraph &) -> void {
                    cb.BindComputeStage(*stage);
                    cb.BindComputeResource(*binding);
                    cb.DispatchCompute(body_wg, 1, 1);
                })
                .Get()
        );

        return builder.BuildRenderGraph();
    }

} // namespace Engine
