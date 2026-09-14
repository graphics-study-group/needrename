#include "XPBDGpuSolver.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/Collision/ConvexCollisionDetector.h>
#include <Physics/Collision/SpatialHashBroadDetector.h>
#include <Physics/PhysicsScene.h>
#include <Physics/gpu_algorithm/RadixSort.h>
#include <Physics/gpu_algorithm/SumByKey.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeHelpers.h>

#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Pipeline/ComputeResourceBinding.h>
#include <Rhi/Pipeline/ComputeStage.h>
#include <Rhi/Pipeline/ShaderResourceBinding.h>

#include <algorithm>
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

    constexpr uint32_t kNumChannels = 7u; // Δlin3 + Δang3 + flag

    // Hinge/fixed joints own 4 entry slots each: 2 bodies x 2 scalar constraints.
    constexpr uint32_t kJointSlotsPerJoint = 4u;

    // Push-constant layouts matching the per-shader blocks (std430).
    struct AccumContactPush { // accumulate_contact_position.comp
        uint32_t entry_capacity;
    };
    static_assert(sizeof(AccumContactPush) == 4);

    struct AccumVelocityPush { // accumulate_contact_velocity.comp
        glm::vec4 gravity_dt;
        uint32_t entry_capacity;
    };
    static_assert(sizeof(AccumVelocityPush) == 20);

    struct AccumJointPush { // accumulate_{hinge,fixed}_position.comp
        glm::vec4 gravity_dt;
        uint32_t joint_count;
        uint32_t entry_capacity;
    };
    static_assert(sizeof(AccumJointPush) == 24);

    struct JointCountPush { // entries/{hinge,fixed}_entries.comp
        uint32_t joint_count;
        uint32_t entry_capacity;
    };
    static_assert(sizeof(JointCountPush) == 8);

    struct ContactEntryPush { // entries/contact_entries.comp
        uint32_t entry_capacity;
    };
    static_assert(sizeof(ContactEntryPush) == 4);

    struct BodyCountPush { // apply_body_*.comp
        uint32_t body_count;
    };
    static_assert(sizeof(BodyCountPush) == 4);
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
        std::unique_ptr<Rhi::ComputeStage> contact_entries_stage{};
        std::unique_ptr<Rhi::ComputeStage> hinge_entries_stage{};
        std::unique_ptr<Rhi::ComputeStage> fixed_entries_stage{};
        std::unique_ptr<Rhi::ComputeStage> accum_pos_stage{};
        std::unique_ptr<Rhi::ComputeStage> apply_pos_stage{};
        std::unique_ptr<Rhi::ComputeStage> update_vel_stage{};
        std::unique_ptr<Rhi::ComputeStage> accum_vel_stage{};
        std::unique_ptr<Rhi::ComputeStage> apply_vel_stage{};
        std::unique_ptr<Rhi::ComputeStage> model_matrix_stage{};
        std::unique_ptr<Rhi::ComputeStage> accum_hinge_stage{};
        std::unique_ptr<Rhi::ComputeStage> accum_fixed_stage{};

        Rhi::ComputeResourceBinding *clear_int_binding = nullptr;
        Rhi::ComputeResourceBinding *snapshot_binding = nullptr;
        Rhi::ComputeResourceBinding *update_shape_world_pose_binding = nullptr;
        Rhi::ComputeResourceBinding *integrate_binding = nullptr;
        Rhi::ComputeResourceBinding *contact_entries_binding = nullptr;
        Rhi::ComputeResourceBinding *hinge_entries_binding = nullptr;
        Rhi::ComputeResourceBinding *fixed_entries_binding = nullptr;
        Rhi::ComputeResourceBinding *accum_pos_binding = nullptr;
        Rhi::ComputeResourceBinding *apply_pos_binding = nullptr;
        Rhi::ComputeResourceBinding *update_vel_binding = nullptr;
        Rhi::ComputeResourceBinding *accum_vel_binding = nullptr;
        Rhi::ComputeResourceBinding *apply_vel_binding = nullptr;
        Rhi::ComputeResourceBinding *model_matrix_binding = nullptr;
        Rhi::ComputeResourceBinding *accum_hinge_binding = nullptr;
        Rhi::ComputeResourceBinding *accum_fixed_binding = nullptr;

        // ---- Intermediate GPU buffers ----
        std::unique_ptr<Rhi::ComputeBuffer> gpu_pre_contact_linear_vel{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_pre_contact_angular_vel{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_substep_start_position{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_substep_start_orientation{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_lagrange{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_axis_lagrange{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_anchor_lagrange{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_rotation_lagrange{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_position_lagrange{};

        // ---- Reduce-group infrastructure ----
        std::unique_ptr<Rhi::ComputeBuffer> gpu_radix_histogram{};

        // Contact (capacity = 2*max_contact_point slots).  One value buffer serves
        // the position and the velocity phase: they are strictly sequential and each
        // clears it before its accumulate (see the velocity loop below).
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_entries{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_entries_tmp{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_entry_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_values{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_reduce_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_out_pos{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_out_vel{};
        std::unique_ptr<RadixSort> contact_sort{};
        std::unique_ptr<SumByKey> contact_sum{};

        // Hinge (capacity = 4*max(1,hinge_count) slots).
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_entries{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_entries_tmp{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_entry_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_values{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_reduce_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_out{};
        std::unique_ptr<RadixSort> hinge_sort{};
        std::unique_ptr<SumByKey> hinge_sum{};

        // Fixed (capacity = 4*max(1,fixed_count) slots).
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_entries{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_entries_tmp{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_entry_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_values{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_reduce_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_out{};
        std::unique_ptr<RadixSort> fixed_sort{};
        std::unique_ptr<SumByKey> fixed_sum{};

        glm::vec4 push_gravity_dt{0.0f, 0.0f, -9.81f, 0.0f};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        void EnsureBuffer(std::unique_ptr<Rhi::ComputeBuffer> &buf, size_t bytes, const char *name) {
            const auto &alloc = device_context.GetAllocatorState();
            if (!buf || buf->GetSize() != bytes) {
                buf = Rhi::ComputeBuffer::CreateUnique(alloc, bytes, false, false, false, false, name);
            }
        }

        struct ReduceGroupBufs {
            std::unique_ptr<Rhi::ComputeBuffer> *entries;
            std::unique_ptr<Rhi::ComputeBuffer> *entries_tmp;
            std::unique_ptr<Rhi::ComputeBuffer> *entry_count;
            std::unique_ptr<Rhi::ComputeBuffer> *values;
            std::unique_ptr<Rhi::ComputeBuffer> *reduce_scratch;
            std::unique_ptr<Rhi::ComputeBuffer> *out;
        };

        // The group's sorted entry array is both the radix sort's in/out buffer and
        // SumByKey's level-0 input, so no separate key array or permutation map is
        // allocated (design.md Decision 6).  `entries_tmp` is the sort's ping-pong
        // temporary, not a second live array; `values` holds the real per-entry
        // contribution values; `reduce_scratch` is internal to SumByKey and holds
        // the boundary records of its intermediate levels.
        void EnsureReduceGroup(uint32_t capacity, uint32_t body_count, const ReduceGroupBufs &g) {
            const size_t cap_bytes = static_cast<size_t>(capacity) * sizeof(uint32_t);
            const size_t entry_bytes = static_cast<size_t>(capacity) * 2u * sizeof(uint32_t);
            EnsureBuffer(*g.entries, entry_bytes, "XPBD ReduceEntries");
            EnsureBuffer(*g.entries_tmp, entry_bytes, "XPBD ReduceEntriesTmp");
            // The entry count must be host-writable (Scheme A sets it to the capacity).
            {
                const auto &alloc = device_context.GetAllocatorState();
                if (!*g.entry_count || (*g.entry_count)->GetSize() != sizeof(uint32_t)) {
                    *g.entry_count = Rhi::ComputeBuffer::CreateUnique(
                        alloc, sizeof(uint32_t), true, false, false, false, "XPBD ReduceEntryCount"
                    );
                }
            }
            EnsureBuffer(*g.values, static_cast<size_t>(kNumChannels) * cap_bytes, "XPBD ReduceValues");
            size_t rec_bytes = SumByKey::GetRequiredRecordsBytes(capacity, kNumChannels);
            if (rec_bytes == 0u) rec_bytes = sizeof(uint32_t);
            EnsureBuffer(*g.reduce_scratch, rec_bytes, "XPBD ReduceRecordRegions");
            EnsureBuffer(*g.out, static_cast<size_t>(kNumChannels) * body_count * sizeof(uint32_t), "XPBD ReduceOut");
        }

        // RadixSort and SumByKey hold no geometry: every call supplies its own
        // capacity, so one instance serves any geometry and never needs
        // rebuilding.  Both are still created lazily, on the first substep that
        // reaches them.
        void EnsureSortAndSum(std::unique_ptr<RadixSort> &sort, std::unique_ptr<SumByKey> &sum) {
            if (!sort) sort = std::make_unique<RadixSort>(device_context);
            if (!sum) sum = std::make_unique<SumByKey>(device_context);
        }

        void SetConstantU32(Rhi::ComputeBuffer &buf, uint32_t value) {
            *reinterpret_cast<uint32_t *>(buf.GetVMAddress()) = value;
            buf.Flush();
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

            contact_entries_stage = load("solver/XPBDSolver/entries/contact_entries.comp.spv", "XPBD ContactEntries");
            contact_entries_binding = &contact_entries_stage->AllocateResourceBinding();

            hinge_entries_stage = load("solver/XPBDSolver/entries/hinge_entries.comp.spv", "XPBD HingeEntries");
            hinge_entries_binding = &hinge_entries_stage->AllocateResourceBinding();

            fixed_entries_stage = load("solver/XPBDSolver/entries/fixed_entries.comp.spv", "XPBD FixedEntries");
            fixed_entries_binding = &fixed_entries_stage->AllocateResourceBinding();

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

            accum_hinge_stage = load("solver/XPBDSolver/accumulate_hinge_position.comp.spv", "XPBD AccumHingePos");
            accum_hinge_binding = &accum_hinge_stage->AllocateResourceBinding();

            accum_fixed_stage = load("solver/XPBDSolver/accumulate_fixed_position.comp.spv", "XPBD AccumFixedPos");
            accum_fixed_binding = &accum_fixed_stage->AllocateResourceBinding();
        }

        void RecordSort(
            vk::CommandBuffer cb,
            RadixSort &sort,
            Rhi::ComputeBuffer &pairs_a,
            Rhi::ComputeBuffer &pairs_b,
            Rhi::ComputeBuffer &count,
            uint32_t capacity
        ) {
            sort.Record(
                cb, pairs_a, pairs_b, *gpu_radix_histogram, capacity, count, 1u << 20u, RadixSortMode::ePrimaryOnly
            );
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

        {
            const auto &alloc = m_impl->device_context.GetAllocatorState();
            if (!m_impl->gpu_radix_histogram) {
                m_impl->gpu_radix_histogram = Rhi::ComputeBuffer::CreateUnique(
                    alloc, RadixSort::GetRequiredScratchBytes(), false, false, false, false, "XPBD RadixHistogram"
                );
            }
            m_impl->EnsureBuffer(
                m_impl->gpu_contact_lagrange,
                static_cast<size_t>(m_impl->max_contact_point) * sizeof(float),
                "XPBD ContactLagrange"
            );
        }
        m_impl->EnsureBuffer(
            m_impl->gpu_pre_contact_linear_vel, static_cast<size_t>(body_count) * sizeof(glm::vec4), "XPBD PreLin"
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_pre_contact_angular_vel, static_cast<size_t>(body_count) * sizeof(glm::vec4), "XPBD PreAng"
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_substep_start_position, static_cast<size_t>(body_count) * sizeof(glm::vec4), "XPBD SubPos"
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_substep_start_orientation, static_cast<size_t>(body_count) * sizeof(glm::vec4), "XPBD SubOri"
        );

        const uint32_t contact_cap = 2u * m_impl->max_contact_point;
        m_impl->EnsureReduceGroup(
            contact_cap,
            body_count,
            {&m_impl->gpu_contact_entries,
             &m_impl->gpu_contact_entries_tmp,
             &m_impl->gpu_contact_entry_count,
             &m_impl->gpu_contact_values,
             &m_impl->gpu_contact_reduce_scratch,
             &m_impl->gpu_contact_out_pos}
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_contact_out_vel,
            static_cast<size_t>(kNumChannels) * body_count * sizeof(uint32_t),
            "XPBD ContactOutVel"
        );

        m_impl->EnsureSortAndSum(m_impl->contact_sort, m_impl->contact_sum);
        m_impl->SetConstantU32(*m_impl->gpu_contact_entry_count, contact_cap);

        // Hinge/fixed groups.  A joint count of zero still gets one joint's worth
        // of slots, so an empty joint list still has a fully written entry list.
        const uint32_t hinge_slots = kJointSlotsPerJoint * std::max(1u, gpu.hinge_joint_count);
        const uint32_t fixed_slots = kJointSlotsPerJoint * std::max(1u, gpu.fixed_joint_count);

        m_impl->EnsureReduceGroup(
            hinge_slots,
            body_count,
            {&m_impl->gpu_hinge_entries,
             &m_impl->gpu_hinge_entries_tmp,
             &m_impl->gpu_hinge_entry_count,
             &m_impl->gpu_hinge_values,
             &m_impl->gpu_hinge_reduce_scratch,
             &m_impl->gpu_hinge_out}
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_hinge_axis_lagrange,
            static_cast<size_t>(std::max(1u, gpu.hinge_joint_count)) * sizeof(float),
            "XPBD HingeAxisLagrange"
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_hinge_anchor_lagrange,
            static_cast<size_t>(std::max(1u, gpu.hinge_joint_count)) * sizeof(float),
            "XPBD HingeAnchorLagrange"
        );
        m_impl->EnsureSortAndSum(m_impl->hinge_sort, m_impl->hinge_sum);
        m_impl->SetConstantU32(*m_impl->gpu_hinge_entry_count, hinge_slots);

        m_impl->EnsureReduceGroup(
            fixed_slots,
            body_count,
            {&m_impl->gpu_fixed_entries,
             &m_impl->gpu_fixed_entries_tmp,
             &m_impl->gpu_fixed_entry_count,
             &m_impl->gpu_fixed_values,
             &m_impl->gpu_fixed_reduce_scratch,
             &m_impl->gpu_fixed_out}
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_fixed_rotation_lagrange,
            static_cast<size_t>(std::max(1u, gpu.fixed_joint_count)) * sizeof(float),
            "XPBD FixedRotLagrange"
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_fixed_position_lagrange,
            static_cast<size_t>(std::max(1u, gpu.fixed_joint_count)) * sizeof(float),
            "XPBD FixedPosLagrange"
        );
        m_impl->EnsureSortAndSum(m_impl->fixed_sort, m_impl->fixed_sum);
        m_impl->SetConstantU32(*m_impl->gpu_fixed_entry_count, fixed_slots);

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

            // Everything capacity-dependent was sized in PreGPUStep; this function
            // only derives dispatch geometry.  A joint count of zero still owns one
            // joint's worth of slots so the entry pass writes a whole entry list.
            const uint32_t contact_cap = 2u * m_impl->max_contact_point;
            const uint32_t contact_pt_wg = (m_impl->max_contact_point + 63u) / 64u;
            const uint32_t hinge_slots = kJointSlotsPerJoint * std::max(1u, gpu.hinge_joint_count);
            const uint32_t fixed_slots = kJointSlotsPerJoint * std::max(1u, gpu.fixed_joint_count);
            const uint32_t hinge_entry_wg = (hinge_slots / kJointSlotsPerJoint + 63u) / 64u;
            const uint32_t fixed_entry_wg = (fixed_slots / kJointSlotsPerJoint + 63u) / 64u;

            const uint32_t hinge_wg = (gpu.hinge_joint_count + 63u) / 64u;
            const uint32_t fixed_wg = (gpu.fixed_joint_count + 63u) / 64u;

            const uint32_t out_pos_elems = kNumChannels * body_count;
            const uint32_t out_wg = (out_pos_elems + 63u) / 64u;
            const uint32_t contact_values_elems = kNumChannels * contact_cap;
            const uint32_t contact_values_wg = (contact_values_elems + 63u) / 64u;
            const uint32_t hinge_values_elems = kNumChannels * hinge_slots;
            const uint32_t hinge_values_wg = (hinge_values_elems + 63u) / 64u;
            const uint32_t fixed_values_elems = kNumChannels * fixed_slots;
            const uint32_t fixed_values_wg = (fixed_values_elems + 63u) / 64u;

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

                // ====== Entry lists: build → sort (all three types) ======
                // The entry passes are dispatched over the entry capacity and write
                // every slot ((key, slot) with INVALID for unowned slots), so the
                // sorted pair array covers the whole capacity every substep.
                {
                    auto &srb = m_impl->contact_entries_binding->GetShaderResourceBinding();
                    srb.BindBuffer("CollisionIds", *m_impl->narrow_detector->GetResultBuffers().collision_ids);
                    srb.BindBuffer("CollisionCount", *m_impl->narrow_detector->GetResultBuffers().collision_count);
                    srb.BindBuffer("ShapeBoundRigidBody", *gpu.shape_bound_rigid_body);
                    srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                    srb.BindBuffer("EntryPairs", *m_impl->gpu_contact_entries);
                    const ContactEntryPush push{contact_cap};
                    Rhi::PushConstants(cb, *m_impl->contact_entries_stage, push);
                    dispatch(*m_impl->contact_entries_stage, *m_impl->contact_entries_binding, contact_pt_wg);
                }
                barrier();
                m_impl->RecordSort(
                    cb,
                    *m_impl->contact_sort,
                    *m_impl->gpu_contact_entries,
                    *m_impl->gpu_contact_entries_tmp,
                    *m_impl->gpu_contact_entry_count,
                    contact_cap
                );
                barrier();

                {
                    auto &srb = m_impl->hinge_entries_binding->GetShaderResourceBinding();
                    srb.BindBuffer("HingeJoints", *gpu.gpu_hinge_joints);
                    srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                    srb.BindBuffer("EntryPairs", *m_impl->gpu_hinge_entries);
                    const JointCountPush push{gpu.hinge_joint_count, hinge_slots};
                    Rhi::PushConstants(cb, *m_impl->hinge_entries_stage, push);
                    dispatch(*m_impl->hinge_entries_stage, *m_impl->hinge_entries_binding, hinge_entry_wg);
                }
                barrier();
                m_impl->RecordSort(
                    cb,
                    *m_impl->hinge_sort,
                    *m_impl->gpu_hinge_entries,
                    *m_impl->gpu_hinge_entries_tmp,
                    *m_impl->gpu_hinge_entry_count,
                    hinge_slots
                );
                barrier();

                {
                    auto &srb = m_impl->fixed_entries_binding->GetShaderResourceBinding();
                    srb.BindBuffer("FixedJoints", *gpu.gpu_fixed_joints);
                    srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                    srb.BindBuffer("EntryPairs", *m_impl->gpu_fixed_entries);
                    const JointCountPush push{gpu.fixed_joint_count, fixed_slots};
                    Rhi::PushConstants(cb, *m_impl->fixed_entries_stage, push);
                    dispatch(*m_impl->fixed_entries_stage, *m_impl->fixed_entries_binding, fixed_entry_wg);
                }
                barrier();
                m_impl->RecordSort(
                    cb,
                    *m_impl->fixed_sort,
                    *m_impl->gpu_fixed_entries,
                    *m_impl->gpu_fixed_entries_tmp,
                    *m_impl->gpu_fixed_entry_count,
                    fixed_slots
                );
                barrier();

                // ====== Clear lagrange multipliers and per-body partials ======
                // The partial-sum outputs are cleared here (once per substep) and
                // re-cleared by apply_* as it consumes them, so a body with no
                // entries always reads zeros.
                dispatch_clear(*m_impl->gpu_contact_lagrange, m_impl->max_contact_point, contact_pt_wg);
                barrier();
                dispatch_clear(*m_impl->gpu_contact_out_pos, out_pos_elems, out_wg);
                barrier();
                dispatch_clear(*m_impl->gpu_contact_out_vel, out_pos_elems, out_wg);
                barrier();
                dispatch_clear(*m_impl->gpu_hinge_out, out_pos_elems, out_wg);
                barrier();
                dispatch_clear(*m_impl->gpu_fixed_out, out_pos_elems, out_wg);
                barrier();
                {
                    size_t n = m_impl->gpu_hinge_axis_lagrange->GetSize() / sizeof(uint32_t);
                    Rhi::PushConstants(cb, *m_impl->clear_int_stage, static_cast<uint32_t>(n));
                    auto &srb = m_impl->clear_int_binding->GetShaderResourceBinding();
                    srb.BindBuffer("Target", *m_impl->gpu_hinge_axis_lagrange);
                    dispatch(
                        *m_impl->clear_int_stage, *m_impl->clear_int_binding, (static_cast<uint32_t>(n) + 63u) / 64u
                    );
                }
                barrier();

                // ====== Position Iterations ======
                for (uint32_t iter = 0; iter < pos_iters; ++iter) {
                    barrier();

                    const auto g = m_bound_scene->GetGpuBuffers();

                    // Contact scatter (position value buffer) — cleared first, so a
                    // non-contributing contact leaves zeroes with flag 0.
                    dispatch_clear(*m_impl->gpu_contact_values, contact_values_elems, contact_values_wg);
                    barrier();
                    {
                        auto &srb = m_impl->accum_pos_binding->GetShaderResourceBinding();
                        srb.BindBuffer("CollisionIds", *m_impl->narrow_detector->GetResultBuffers().collision_ids);
                        srb.BindBuffer(
                            "CollisionNormals", *m_impl->narrow_detector->GetResultBuffers().collision_normals
                        );
                        srb.BindBuffer("ContactPointA", *m_impl->narrow_detector->GetResultBuffers().contact_point_a);
                        srb.BindBuffer("ContactPointB", *m_impl->narrow_detector->GetResultBuffers().contact_point_b);
                        srb.BindBuffer("CollisionCount", *m_impl->narrow_detector->GetResultBuffers().collision_count);
                        srb.BindBuffer("ShapeBoundRigidBody", *g.shape_bound_rigid_body);
                        srb.BindBuffer("RigidBodyAlive", *g.rigid_body_alive);
                        srb.BindBuffer("RigidBodyCenterPosition", *g.rigid_body_center_world_position);
                        srb.BindBuffer("RigidBodyCenterRotation", *g.rigid_body_center_world_rotation);
                        srb.BindBuffer("RigidBodyMass", *g.rigid_body_mass);
                        srb.BindBuffer("RigidBodyInverseInertia", *g.rigid_body_inverse_inertia);
                        srb.BindBuffer("RigidBodyIsKinematic", *g.rigid_body_is_kinematic);
                        srb.BindBuffer("ShapeLocalPosition", *g.shape_local_position);
                        srb.BindBuffer("ShapeLocalRotation", *g.shape_local_rotation);
                        srb.BindBuffer("ContactLagrange", *m_impl->gpu_contact_lagrange);
                        srb.BindBuffer("Values", *m_impl->gpu_contact_values);
                        const AccumContactPush push{contact_cap};
                        Rhi::PushConstants(cb, *m_impl->accum_pos_stage, push);
                        dispatch(*m_impl->accum_pos_stage, *m_impl->accum_pos_binding, contact_pt_wg);
                    }
                    barrier();
                    m_impl->contact_sum->Record(
                        cb,
                        *m_impl->gpu_contact_entries,
                        *m_impl->gpu_contact_values,
                        *m_impl->gpu_contact_reduce_scratch,
                        *m_impl->gpu_contact_out_pos,
                        *m_impl->gpu_contact_entry_count,
                        contact_cap,
                        kNumChannels,
                        body_count
                    );
                    barrier();

                    // Hinge scatter + reduce.
                    dispatch_clear(*m_impl->gpu_hinge_values, hinge_values_elems, hinge_values_wg);
                    barrier();
                    if (gpu.hinge_joint_count > 0u) {
                        auto &srb = m_impl->accum_hinge_binding->GetShaderResourceBinding();
                        srb.BindBuffer("HingeJoints", *gpu.gpu_hinge_joints);
                        srb.BindBuffer("HingeAxisLagrange", *m_impl->gpu_hinge_axis_lagrange);
                        srb.BindBuffer("HingeAnchorLagrange", *m_impl->gpu_hinge_anchor_lagrange);
                        srb.BindBuffer("HingeJointAlive", *gpu.gpu_hinge_joint_alive);
                        srb.BindBuffer("RigidBodyAlive", *g.rigid_body_alive);
                        srb.BindBuffer("RigidBodyCenterPosition", *g.rigid_body_center_world_position);
                        srb.BindBuffer("RigidBodyCenterRotation", *g.rigid_body_center_world_rotation);
                        srb.BindBuffer("RigidBodyMass", *g.rigid_body_mass);
                        srb.BindBuffer("RigidBodyInverseInertia", *g.rigid_body_inverse_inertia);
                        srb.BindBuffer("RigidBodyIsKinematic", *g.rigid_body_is_kinematic);
                        srb.BindBuffer("Values", *m_impl->gpu_hinge_values);
                        const AccumJointPush push{m_impl->push_gravity_dt, gpu.hinge_joint_count, hinge_slots};
                        Rhi::PushConstants(cb, *m_impl->accum_hinge_stage, push);
                        dispatch(*m_impl->accum_hinge_stage, *m_impl->accum_hinge_binding, hinge_wg);
                    }
                    barrier();
                    m_impl->hinge_sum->Record(
                        cb,
                        *m_impl->gpu_hinge_entries,
                        *m_impl->gpu_hinge_values,
                        *m_impl->gpu_hinge_reduce_scratch,
                        *m_impl->gpu_hinge_out,
                        *m_impl->gpu_hinge_entry_count,
                        hinge_slots,
                        kNumChannels,
                        body_count
                    );
                    barrier();

                    // Fixed scatter + reduce.
                    dispatch_clear(*m_impl->gpu_fixed_values, fixed_values_elems, fixed_values_wg);
                    barrier();
                    if (gpu.fixed_joint_count > 0u) {
                        auto &srb = m_impl->accum_fixed_binding->GetShaderResourceBinding();
                        srb.BindBuffer("FixedJoints", *gpu.gpu_fixed_joints);
                        srb.BindBuffer("FixedRotationLagrange", *m_impl->gpu_fixed_rotation_lagrange);
                        srb.BindBuffer("FixedPositionLagrange", *m_impl->gpu_fixed_position_lagrange);
                        srb.BindBuffer("FixedJointAlive", *gpu.gpu_fixed_joint_alive);
                        srb.BindBuffer("RigidBodyAlive", *g.rigid_body_alive);
                        srb.BindBuffer("RigidBodyCenterPosition", *g.rigid_body_center_world_position);
                        srb.BindBuffer("RigidBodyCenterRotation", *g.rigid_body_center_world_rotation);
                        srb.BindBuffer("RigidBodyMass", *g.rigid_body_mass);
                        srb.BindBuffer("RigidBodyInverseInertia", *g.rigid_body_inverse_inertia);
                        srb.BindBuffer("RigidBodyIsKinematic", *g.rigid_body_is_kinematic);
                        srb.BindBuffer("Values", *m_impl->gpu_fixed_values);
                        const AccumJointPush push{m_impl->push_gravity_dt, gpu.fixed_joint_count, fixed_slots};
                        Rhi::PushConstants(cb, *m_impl->accum_fixed_stage, push);
                        dispatch(*m_impl->accum_fixed_stage, *m_impl->accum_fixed_binding, fixed_wg);
                    }
                    barrier();
                    m_impl->fixed_sum->Record(
                        cb,
                        *m_impl->gpu_fixed_entries,
                        *m_impl->gpu_fixed_values,
                        *m_impl->gpu_fixed_reduce_scratch,
                        *m_impl->gpu_fixed_out,
                        *m_impl->gpu_fixed_entry_count,
                        fixed_slots,
                        kNumChannels,
                        body_count
                    );
                    barrier();

                    // Merged position apply.
                    {
                        auto &srb = m_impl->apply_pos_binding->GetShaderResourceBinding();
                        srb.BindBuffer("RigidBodyAlive", *g.rigid_body_alive);
                        srb.BindBuffer("RigidBodyCenterPosition", *g.rigid_body_center_world_position);
                        srb.BindBuffer("RigidBodyCenterRotation", *g.rigid_body_center_world_rotation);
                        srb.BindBuffer("RigidBodyIsKinematic", *g.rigid_body_is_kinematic);
                        srb.BindBuffer("ContactOut", *m_impl->gpu_contact_out_pos);
                        srb.BindBuffer("HingeOut", *m_impl->gpu_hinge_out);
                        srb.BindBuffer("FixedOut", *m_impl->gpu_fixed_out);
                        const BodyCountPush push{body_count};
                        Rhi::PushConstants(cb, *m_impl->apply_pos_stage, push);
                        dispatch(*m_impl->apply_pos_stage, *m_impl->apply_pos_binding, body_wg);
                    }
                }

                // ====== PostPosition: update velocities from pose ======
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

                // ====== Velocity iterations (reuse contact permutation) ======
                for (uint32_t iter = 0; iter < vel_iters; ++iter) {
                    barrier();
                    // Same value buffer as the position phase.  That phase has
                    // finished (its values were consumed by the last apply), and
                    // this clear erases them before the velocity scatter, so no
                    // position-phase value can reach the velocity reduction.
                    dispatch_clear(*m_impl->gpu_contact_values, contact_values_elems, contact_values_wg);
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
                        srb.BindBuffer("ContactLagrange", *m_impl->gpu_contact_lagrange);
                        srb.BindBuffer("Values", *m_impl->gpu_contact_values);
                        const AccumVelocityPush push{m_impl->push_gravity_dt, contact_cap};
                        Rhi::PushConstants(cb, *m_impl->accum_vel_stage, push);
                        dispatch(*m_impl->accum_vel_stage, *m_impl->accum_vel_binding, contact_pt_wg);
                    }
                    barrier();
                    m_impl->contact_sum->Record(
                        cb,
                        *m_impl->gpu_contact_entries,
                        *m_impl->gpu_contact_values,
                        *m_impl->gpu_contact_reduce_scratch,
                        *m_impl->gpu_contact_out_vel,
                        *m_impl->gpu_contact_entry_count,
                        contact_cap,
                        kNumChannels,
                        body_count
                    );
                    barrier();
                    {
                        auto &srb = m_impl->apply_vel_binding->GetShaderResourceBinding();
                        srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
                        srb.BindBuffer("RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic);
                        srb.BindBuffer("RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity);
                        srb.BindBuffer("RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity);
                        srb.BindBuffer("VelOut", *m_impl->gpu_contact_out_vel);
                        const BodyCountPush push{body_count};
                        Rhi::PushConstants(cb, *m_impl->apply_vel_stage, push);
                        dispatch(*m_impl->apply_vel_stage, *m_impl->apply_vel_binding, body_wg);
                    }
                }
            }
        }

        // ====== ModelMatrix (unchanged) ======
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
