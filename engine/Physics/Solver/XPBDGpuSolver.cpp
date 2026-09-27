#include "XPBDGpuSolver.h"

#include <vulkan/vulkan.hpp>

#include <Physics/Collision/ConvexCollisionDetector.h>
#include <Physics/Collision/SpatialHashBroadDetector.h>
#include <Physics/PhysicsScene.h>
#include <Physics/PhysicsSpirvLoader.h>
#include <Physics/gpu_algorithm/RadixSort.h>
#include <Physics/gpu_algorithm/SumByKey.h>
#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeKernel.h>

#include <algorithm>
#include <cassert>
#include <vector>

namespace {
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

    struct ClearEntryValuesPush { // clear_entry_values.comp
        uint32_t entry_capacity;  // channel stride
        uint32_t num_channels;
    };
    static_assert(sizeof(ClearEntryValuesPush) == 8);

    struct ClearJointLagrangePush { // clear_{hinge,fixed}_lagrange.comp
        uint32_t joint_count;
    };
    static_assert(sizeof(ClearJointLagrangePush) == 4);

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

        // ---- Compute kernels (device-owned, acquired in PreGPUStep) ----
        Rhi::ComputeKernel *clear_int_kernel = nullptr;
        Rhi::ComputeKernel *clear_values_kernel = nullptr;
        Rhi::ComputeKernel *clear_hinge_kernel = nullptr;
        Rhi::ComputeKernel *clear_fixed_kernel = nullptr;
        Rhi::ComputeKernel *snapshot_kernel = nullptr;
        Rhi::ComputeKernel *update_shape_world_pose_kernel = nullptr;
        Rhi::ComputeKernel *integrate_kernel = nullptr;
        Rhi::ComputeKernel *contact_entries_kernel = nullptr;
        Rhi::ComputeKernel *hinge_entries_kernel = nullptr;
        Rhi::ComputeKernel *fixed_entries_kernel = nullptr;
        Rhi::ComputeKernel *accum_pos_kernel = nullptr;
        Rhi::ComputeKernel *apply_pos_kernel = nullptr;
        Rhi::ComputeKernel *update_vel_kernel = nullptr;
        Rhi::ComputeKernel *accum_vel_kernel = nullptr;
        Rhi::ComputeKernel *apply_vel_kernel = nullptr;
        Rhi::ComputeKernel *model_matrix_kernel = nullptr;
        Rhi::ComputeKernel *accum_hinge_kernel = nullptr;
        Rhi::ComputeKernel *accum_fixed_kernel = nullptr;

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
        // One sort and one reduction serve every constraint type: both are
        // geometry-free, so each group's capacity and entry count travel with its
        // own call and no instance ever needs rebuilding.
        std::unique_ptr<RadixSort> radix_sort{};
        std::unique_ptr<SumByKey> sum_by_key{};

        // Contact (capacity = 2*max_contact_point slots).  One value buffer serves
        // the position and the velocity phase: they are strictly sequential and each
        // clears it before its accumulate (see the velocity loop below).
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_keys{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_keys_tmp{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_payload{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_payload_tmp{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_entry_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_values{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_reduce_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_radix_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_out_pos{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_out_vel{};

        // Hinge (capacity = 4*max(1,hinge_count) slots).
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_keys{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_keys_tmp{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_payload{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_payload_tmp{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_entry_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_values{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_reduce_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_radix_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_hinge_out{};

        // Fixed (capacity = 4*max(1,fixed_count) slots).
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_keys{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_keys_tmp{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_payload{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_payload_tmp{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_entry_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_values{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_reduce_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_radix_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_fixed_out{};

        glm::vec4 push_gravity_dt{0.0f, 0.0f, -9.81f, 0.0f};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        /// @brief Exact-size resize that keeps the buffer object at the same address.
        void EnsureBuffer(std::unique_ptr<Rhi::ComputeBuffer> &buf, size_t bytes, const char *name) {
            const auto &alloc = device_context.GetAllocatorState();
            if (!buf) {
                buf = Rhi::ComputeBuffer::CreateUnique(alloc, bytes, false, false, false, false, name);
            } else if (buf->GetSize() != bytes) {
                buf->Reallocate(alloc, bytes);
            }
        }

        struct ReduceGroupBufs {
            std::unique_ptr<Rhi::ComputeBuffer> *keys;
            std::unique_ptr<Rhi::ComputeBuffer> *keys_tmp;
            std::unique_ptr<Rhi::ComputeBuffer> *payload;
            std::unique_ptr<Rhi::ComputeBuffer> *payload_tmp;
            std::unique_ptr<Rhi::ComputeBuffer> *entry_count;
            std::unique_ptr<Rhi::ComputeBuffer> *values;
            std::unique_ptr<Rhi::ComputeBuffer> *reduce_scratch;
            std::unique_ptr<Rhi::ComputeBuffer> *radix_scratch;
            std::unique_ptr<Rhi::ComputeBuffer> *out;
        };

        // A group's sorted entry list is a struct-of-arrays record: a key array
        // plus a payload array holding each entry's own slot id.  The sort owns no
        // geometry, so `keys`/`keys_tmp` and `payload`/`payload_tmp` are its
        // ping-pong pairs and `radix_scratch` is its transposed-histogram scratch;
        // the sorted buffers it returns are what SumByKey's level-0 gather reads.
        // `values` holds the real per-entry contribution values and
        // `reduce_scratch` is internal to SumByKey (its boundary records).
        void EnsureReduceGroup(uint32_t capacity, uint32_t body_count, const ReduceGroupBufs &g) {
            const size_t cap_bytes = static_cast<size_t>(capacity) * sizeof(uint32_t);
            EnsureBuffer(*g.keys, cap_bytes, "XPBD ReduceKeys");
            EnsureBuffer(*g.keys_tmp, cap_bytes, "XPBD ReduceKeysTmp");
            EnsureBuffer(*g.payload, cap_bytes, "XPBD ReducePayload");
            EnsureBuffer(*g.payload_tmp, cap_bytes, "XPBD ReducePayloadTmp");
            // The entry count must be host-writable (the hinge/fixed groups set it
            // from the joint count; the contact group's entry pass publishes it).
            {
                const auto &alloc = device_context.GetAllocatorState();
                if (!*g.entry_count) {
                    *g.entry_count = Rhi::ComputeBuffer::CreateUnique(
                        alloc, sizeof(uint32_t), true, false, false, false, "XPBD ReduceEntryCount"
                    );
                } else if ((*g.entry_count)->GetSize() != sizeof(uint32_t)) {
                    (*g.entry_count)->Reallocate(alloc, sizeof(uint32_t));
                }
            }
            EnsureBuffer(*g.values, static_cast<size_t>(kNumChannels) * cap_bytes, "XPBD ReduceValues");
            size_t rec_bytes = SumByKey::GetRequiredRecordsBytes(capacity, kNumChannels);
            if (rec_bytes == 0u) rec_bytes = sizeof(uint32_t);
            EnsureBuffer(*g.reduce_scratch, rec_bytes, "XPBD ReduceRecordRegions");
            EnsureBuffer(*g.radix_scratch, RadixSort::GetRequiredScratchBytes(capacity), "XPBD ReduceRadixScratch");
            EnsureBuffer(*g.out, static_cast<size_t>(kNumChannels) * body_count * sizeof(uint32_t), "XPBD ReduceOut");
        }

        void SetConstantU32(Rhi::ComputeBuffer &buf, uint32_t value) {
            *reinterpret_cast<uint32_t *>(buf.GetVMAddress()) = value;
            buf.Flush();
        }

        void EnsureShadersLoaded() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            auto load = [this](const char *path, const char *name) {
                return &LoadPhysicsKernel(device_context, path, name);
            };

            clear_int_kernel = load("solver/XPBDSolver/clear_int_buffer.comp.spv", "XPBD Clear Int");
            clear_values_kernel = load("solver/XPBDSolver/clear_entry_values.comp.spv", "XPBD ClearEntryValues");

            // The hinge and fixed lagrange multipliers hold floats and are cleared
            // by their own shaders (each zeroes both of its type's buffers) rather
            // than by the integer-clear workaround.
            clear_hinge_kernel = load("solver/XPBDSolver/clear_hinge_lagrange.comp.spv", "XPBD ClearHingeLagrange");
            clear_fixed_kernel = load("solver/XPBDSolver/clear_fixed_lagrange.comp.spv", "XPBD ClearFixedLagrange");

            snapshot_kernel = load("solver/XPBDSolver/snapshot_position.comp.spv", "XPBD Snapshot");
            update_shape_world_pose_kernel =
                load("solver/XPBDSolver/update_shape_world_pose.comp.spv", "XPBD UpdateShape");
            integrate_kernel = load("solver/XPBDSolver/integrate_forces.comp.spv", "XPBD Integrate");
            accum_pos_kernel = load("solver/XPBDSolver/accumulate_contact_position.comp.spv", "XPBD AccumPos");
            contact_entries_kernel = load("solver/XPBDSolver/entries/contact_entries.comp.spv", "XPBD ContactEntries");
            hinge_entries_kernel = load("solver/XPBDSolver/entries/hinge_entries.comp.spv", "XPBD HingeEntries");
            fixed_entries_kernel = load("solver/XPBDSolver/entries/fixed_entries.comp.spv", "XPBD FixedEntries");
            apply_pos_kernel = load("solver/XPBDSolver/apply_body_position_deltas.comp.spv", "XPBD ApplyPos");
            update_vel_kernel = load("solver/XPBDSolver/update_velocities_from_pose.comp.spv", "XPBD UpdateVel");
            accum_vel_kernel = load("solver/XPBDSolver/accumulate_contact_velocity.comp.spv", "XPBD AccumVel");
            apply_vel_kernel = load("solver/XPBDSolver/apply_body_velocity_deltas.comp.spv", "XPBD ApplyVel");
            model_matrix_kernel = load("solver/common/model_matrix.comp.spv", "XPBD ModelMatrix");
            accum_hinge_kernel = load("solver/XPBDSolver/accumulate_hinge_position.comp.spv", "XPBD AccumHingePos");
            accum_fixed_kernel = load("solver/XPBDSolver/accumulate_fixed_position.comp.spv", "XPBD AccumFixedPos");
        }

        // Sorts one group's entry list.  The shared radix sort takes the group's
        // capacity and key bound per call and reads the group's entry count at
        // execution time; it returns the buffers its last pass wrote, because the
        // derived pass count decides which of the two ping-pong arrays holds the
        // result.
        //
        // The key domain is host-known and tight: every key is a body index
        // (`< body_count`) or the invalid-slot sentinel (`== body_count`), so
        // `max_key_value = body_count` covers all of them and keeps the entry
        // count a prefix of the sorted order.
        RadixSortOutput RecordSort(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &keys,
            Rhi::ComputeBuffer &keys_tmp,
            Rhi::ComputeBuffer &payload,
            Rhi::ComputeBuffer &payload_tmp,
            Rhi::ComputeBuffer &radix_scratch,
            Rhi::ComputeBuffer &entry_count,
            uint32_t capacity,
            uint32_t body_count
        ) {
            assert(body_count > 0u && "the entry shaders use body_count as their invalid-slot key");
            const RadixSortBuffers buffers{
                .keys_a = &keys,
                .keys_b = &keys_tmp,
                .payload_a = &payload,
                .payload_b = &payload_tmp,
                .scratch = &radix_scratch,
                .count = &entry_count,
            };
            return radix_sort->Record(cb, buffers, capacity, body_count);
        }

        // Reduces one group's contribution values into its per-body output.  The
        // shared reducer takes the whole geometry per call, gathers through the
        // payload array the sort returned, and reads the group's entry count at
        // execution time.
        void RecordReduce(
            vk::CommandBuffer cb,
            const RadixSortOutput &sorted,
            Rhi::ComputeBuffer &values,
            Rhi::ComputeBuffer &reduce_scratch,
            Rhi::ComputeBuffer &out,
            Rhi::ComputeBuffer &entry_count,
            uint32_t capacity,
            uint32_t body_count
        ) {
            assert(sorted.keys != nullptr && sorted.payload != nullptr);
            sum_by_key->Record(
                cb,
                *sorted.keys,
                *sorted.payload,
                values,
                reduce_scratch,
                out,
                entry_count,
                capacity,
                kNumChannels,
                body_count
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
            {&m_impl->gpu_contact_keys,
             &m_impl->gpu_contact_keys_tmp,
             &m_impl->gpu_contact_payload,
             &m_impl->gpu_contact_payload_tmp,
             &m_impl->gpu_contact_entry_count,
             &m_impl->gpu_contact_values,
             &m_impl->gpu_contact_reduce_scratch,
             &m_impl->gpu_contact_radix_scratch,
             &m_impl->gpu_contact_out_pos}
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_contact_out_vel,
            static_cast<size_t>(kNumChannels) * body_count * sizeof(uint32_t),
            "XPBD ContactOutVel"
        );

        // One sort and one reduction serve every constraint type.  Both are
        // geometry-free, so they are created once and never rebuilt: the group's
        // capacity and entry count travel with each call.
        if (!m_impl->radix_sort) m_impl->radix_sort = std::make_unique<RadixSort>(m_impl->device_context);
        if (!m_impl->sum_by_key) m_impl->sum_by_key = std::make_unique<SumByKey>(m_impl->device_context);
        // The contact entry count is published on the GPU by the entry pass, from
        // the collision count it reads in the same substep, so the host must not
        // write it (nor disable the radix sort's count guard, as it used to).

        // Hinge/fixed groups.  A joint count of zero still gets one joint's worth
        // of slots (the capacity keeps its max(1, ...) floor) so an empty joint
        // list still has a fully written entry list; the *count* published below
        // is the exact joint slot count, so the reduction ignores that group
        // outright instead of reading a slot group owned by no joint.
        const uint32_t hinge_slots = kJointSlotsPerJoint * std::max(1u, gpu.hinge_joint_count);
        const uint32_t fixed_slots = kJointSlotsPerJoint * std::max(1u, gpu.fixed_joint_count);

        m_impl->EnsureReduceGroup(
            hinge_slots,
            body_count,
            {&m_impl->gpu_hinge_keys,
             &m_impl->gpu_hinge_keys_tmp,
             &m_impl->gpu_hinge_payload,
             &m_impl->gpu_hinge_payload_tmp,
             &m_impl->gpu_hinge_entry_count,
             &m_impl->gpu_hinge_values,
             &m_impl->gpu_hinge_reduce_scratch,
             &m_impl->gpu_hinge_radix_scratch,
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
        m_impl->SetConstantU32(*m_impl->gpu_hinge_entry_count, kJointSlotsPerJoint * gpu.hinge_joint_count);

        m_impl->EnsureReduceGroup(
            fixed_slots,
            body_count,
            {&m_impl->gpu_fixed_keys,
             &m_impl->gpu_fixed_keys_tmp,
             &m_impl->gpu_fixed_payload,
             &m_impl->gpu_fixed_payload_tmp,
             &m_impl->gpu_fixed_entry_count,
             &m_impl->gpu_fixed_values,
             &m_impl->gpu_fixed_reduce_scratch,
             &m_impl->gpu_fixed_radix_scratch,
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
        m_impl->SetConstantU32(*m_impl->gpu_fixed_entry_count, kJointSlotsPerJoint * gpu.fixed_joint_count);

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

        auto dispatch_clear = [this, &cb](Rhi::ComputeBuffer &tgt, uint32_t elem_count, uint32_t wg) {
            m_impl->clear_int_kernel->Dispatch(
                cb, {{"Target", Rhi::ComputeKernelResource::Buffer(tgt)}}, wg, 1, 1, elem_count
            );
        };

        // Count-bounded clear of one group's per-iteration value buffer: only the
        // slots below that substep's entry count are written.  The dispatch
        // geometry stays the flat clear's (the count is produced on the GPU), so
        // this replaces the full-capacity clear without changing its workgroup
        // count.
        auto dispatch_clear_values =
            [this, &cb](Rhi::ComputeBuffer &values, Rhi::ComputeBuffer &entry_count, uint32_t capacity, uint32_t wg) {
                const ClearEntryValuesPush push{capacity, kNumChannels};
                m_impl->clear_values_kernel->Dispatch(
                    cb,
                    {{"Values", Rhi::ComputeKernelResource::Buffer(values)},
                     {"EntryCount", Rhi::ComputeKernelResource::Buffer(entry_count)}},
                    wg,
                    1,
                    1,
                    push
                );
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
            // The counted clear's dispatch geometry is still capacity-derived (the
            // count is only known on the GPU); the shader bounds the writes.
            const uint32_t contact_values_wg = (kNumChannels * contact_cap + 63u) / 64u;
            const uint32_t hinge_values_wg = (kNumChannels * hinge_slots + 63u) / 64u;
            const uint32_t fixed_values_wg = (kNumChannels * fixed_slots + 63u) / 64u;

            for (uint32_t ss = 0; ss < substep_count; ++ss) {
                // ====== PreCollision ======
                barrier();

                {
                    m_impl->snapshot_kernel->Dispatch(
                        cb,
                        {{"SrcBuffer", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_center_world_position)},
                         {"DstBuffer", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_substep_start_position)}},
                        body_wg,
                        1,
                        1,
                        body_count
                    );
                }
                barrier();

                {
                    m_impl->snapshot_kernel->Dispatch(
                        cb,
                        {{"SrcBuffer", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_center_world_rotation)},
                         {"DstBuffer", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_substep_start_orientation)}},
                        body_wg,
                        1,
                        1,
                        body_count
                    );
                }
                barrier();

                {
                    m_impl->integrate_kernel->Dispatch(
                        cb,
                        {{"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_alive)},
                         {"RigidBodyCenterPosition",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_center_world_position)},
                         {"RigidBodyCenterRotation",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_center_world_rotation)},
                         {"RigidBodyLinearVelocity",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_linear_velocity)},
                         {"RigidBodyAngularVelocity",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_angular_velocity)},
                         {"RigidBodyMass", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_mass)},
                         {"RigidBodyInverseInertia",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_inverse_inertia)},
                         {"RigidBodyInertia", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_inertia)},
                         {"RigidBodyExternalForce", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_external_force)},
                         {"RigidBodyExternalTorque",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_external_torque)},
                         {"RigidBodyIsKinematic", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_is_kinematic)}},
                        body_wg,
                        1,
                        1,
                        m_impl->push_gravity_dt
                    );
                }
                barrier();

                {
                    m_impl->snapshot_kernel->Dispatch(
                        cb,
                        {{"SrcBuffer", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_linear_velocity)},
                         {"DstBuffer", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_pre_contact_linear_vel)}},
                        body_wg,
                        1,
                        1,
                        body_count
                    );
                }
                barrier();

                {
                    m_impl->snapshot_kernel->Dispatch(
                        cb,
                        {{"SrcBuffer", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_angular_velocity)},
                         {"DstBuffer", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_pre_contact_angular_vel)}},
                        body_wg,
                        1,
                        1,
                        body_count
                    );
                }
                barrier();

                if (shape_count > 1u && gpu.shape_world_position != nullptr) {
                    m_impl->update_shape_world_pose_kernel->Dispatch(
                        cb,
                        {{"ShapeAlive", Rhi::ComputeKernelResource::Buffer(*gpu.shape_alive)},
                         {"ShapeBoundRigidBody", Rhi::ComputeKernelResource::Buffer(*gpu.shape_bound_rigid_body)},
                         {"ShapeLocalPosition", Rhi::ComputeKernelResource::Buffer(*gpu.shape_local_position)},
                         {"ShapeLocalRotation", Rhi::ComputeKernelResource::Buffer(*gpu.shape_local_rotation)},
                         {"RigidBodyCenterPosition",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_center_world_position)},
                         {"RigidBodyCenterRotation",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_center_world_rotation)},
                         {"ShapeWorldPosition", Rhi::ComputeKernelResource::Buffer(*gpu.shape_world_position)},
                         {"ShapeWorldRotation", Rhi::ComputeKernelResource::Buffer(*gpu.shape_world_rotation)}},
                        shape_wg,
                        1,
                        1
                    );
                }

                // ====== Collision Detection ======
                m_impl->broad_detector->Record(cb);
                m_impl->narrow_detector->Record(cb);

                // ====== PostCollision PreIter ======
                barrier();

                // ====== Entry lists: build → sort (all three types) ======
                // The entry passes are dispatched over the entry capacity and write
                // every slot on every dispatch (a key — body index or the
                // `body_count` invalid-slot key — plus the slot's own id as its
                // payload), so the sorted key array covers the whole capacity every
                // substep.  The sort's derived pass count decides which of the two
                // ping-pong arrays holds the result, so each sort's return value is
                // what the reduction below is bound to.
                RadixSortOutput contact_sorted{};
                RadixSortOutput hinge_sorted{};
                RadixSortOutput fixed_sorted{};
                {
                    const ContactEntryPush push{contact_cap};
                    m_impl->contact_entries_kernel->Dispatch(
                        cb,
                        {{"CollisionIds",
                          Rhi::ComputeKernelResource::Buffer(
                              *m_impl->narrow_detector->GetResultBuffers().collision_ids
                          )},
                         {"CollisionCount",
                          Rhi::ComputeKernelResource::Buffer(
                              *m_impl->narrow_detector->GetResultBuffers().collision_count
                          )},
                         {"ShapeBoundRigidBody", Rhi::ComputeKernelResource::Buffer(*gpu.shape_bound_rigid_body)},
                         {"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_alive)},
                         {"EntryKeys", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_keys)},
                         {"EntryPayload", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_payload)},
                         // The entry pass derives and publishes the substep's entry
                         // count from the collision count it reads here.
                         {"EntryCount", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_entry_count)}},
                        contact_pt_wg,
                        1,
                        1,
                        push
                    );
                }
                barrier();
                contact_sorted = m_impl->RecordSort(
                    cb,
                    *m_impl->gpu_contact_keys,
                    *m_impl->gpu_contact_keys_tmp,
                    *m_impl->gpu_contact_payload,
                    *m_impl->gpu_contact_payload_tmp,
                    *m_impl->gpu_contact_radix_scratch,
                    *m_impl->gpu_contact_entry_count,
                    contact_cap,
                    body_count
                );
                barrier();

                {
                    const JointCountPush push{gpu.hinge_joint_count, hinge_slots};
                    m_impl->hinge_entries_kernel->Dispatch(
                        cb,
                        {{"HingeJoints", Rhi::ComputeKernelResource::Buffer(*gpu.gpu_hinge_joints)},
                         {"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_alive)},
                         {"EntryKeys", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_hinge_keys)},
                         {"EntryPayload", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_hinge_payload)}},
                        hinge_entry_wg,
                        1,
                        1,
                        push
                    );
                }
                barrier();
                hinge_sorted = m_impl->RecordSort(
                    cb,
                    *m_impl->gpu_hinge_keys,
                    *m_impl->gpu_hinge_keys_tmp,
                    *m_impl->gpu_hinge_payload,
                    *m_impl->gpu_hinge_payload_tmp,
                    *m_impl->gpu_hinge_radix_scratch,
                    *m_impl->gpu_hinge_entry_count,
                    hinge_slots,
                    body_count
                );
                barrier();

                {
                    const JointCountPush push{gpu.fixed_joint_count, fixed_slots};
                    m_impl->fixed_entries_kernel->Dispatch(
                        cb,
                        {{"FixedJoints", Rhi::ComputeKernelResource::Buffer(*gpu.gpu_fixed_joints)},
                         {"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_alive)},
                         {"EntryKeys", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_fixed_keys)},
                         {"EntryPayload", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_fixed_payload)}},
                        fixed_entry_wg,
                        1,
                        1,
                        push
                    );
                }
                barrier();
                fixed_sorted = m_impl->RecordSort(
                    cb,
                    *m_impl->gpu_fixed_keys,
                    *m_impl->gpu_fixed_keys_tmp,
                    *m_impl->gpu_fixed_payload,
                    *m_impl->gpu_fixed_payload_tmp,
                    *m_impl->gpu_fixed_radix_scratch,
                    *m_impl->gpu_fixed_entry_count,
                    fixed_slots,
                    body_count
                );
                barrier();

                // ====== Clear lagrange multipliers and per-body partials ======
                // Every constraint type's lagrange multiplier is zeroed once per
                // substep, by that type's own shader, before the position
                // iterations: the contact multiplier as a flat float range, and the
                // hinge/fixed multipliers by the two shaders that each zero both
                // buffers of their type.  These buffers are read-modify-written
                // across iterations, so a multiplier that were not cleared would
                // accumulate across substeps without bound.
                //
                // The partial-sum outputs are cleared here too (once per substep)
                // and re-cleared by apply_* as it consumes them, so a body with no
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
                    m_impl->clear_hinge_kernel->Dispatch(
                        cb,
                        {{"HingeAxisLagrange", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_hinge_axis_lagrange)},
                         {"HingeAnchorLagrange",
                          Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_hinge_anchor_lagrange)}},
                        std::max(1u, (gpu.hinge_joint_count + 255u) / 256u),
                        1,
                        1,
                        ClearJointLagrangePush{gpu.hinge_joint_count}
                    );
                }
                barrier();
                {
                    m_impl->clear_fixed_kernel->Dispatch(
                        cb,
                        {{"FixedRotationLagrange",
                          Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_fixed_rotation_lagrange)},
                         {"FixedPositionLagrange",
                          Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_fixed_position_lagrange)}},
                        std::max(1u, (gpu.fixed_joint_count + 255u) / 256u),
                        1,
                        1,
                        ClearJointLagrangePush{gpu.fixed_joint_count}
                    );
                }
                barrier();

                // ====== Position Iterations ======
                for (uint32_t iter = 0; iter < pos_iters; ++iter) {
                    barrier();

                    const auto g = m_bound_scene->GetGpuBuffers();

                    // Contact scatter (position value buffer) — cleared first, so a
                    // non-contributing contact leaves zeroes with flag 0.  The clear
                    // is count-bounded: only slots below the entry count can be
                    // written this iteration, and only those are read back.
                    dispatch_clear_values(
                        *m_impl->gpu_contact_values, *m_impl->gpu_contact_entry_count, contact_cap, contact_values_wg
                    );
                    barrier();
                    {
                        const AccumContactPush push{contact_cap};
                        m_impl->accum_pos_kernel->Dispatch(
                            cb,
                            {{"CollisionIds",
                              Rhi::ComputeKernelResource::Buffer(
                                  *m_impl->narrow_detector->GetResultBuffers().collision_ids
                              )},
                             {"CollisionNormals",
                              Rhi::ComputeKernelResource::Buffer(
                                  *m_impl->narrow_detector->GetResultBuffers().collision_normals
                              )},
                             {"ContactPointA",
                              Rhi::ComputeKernelResource::Buffer(
                                  *m_impl->narrow_detector->GetResultBuffers().contact_point_a
                              )},
                             {"ContactPointB",
                              Rhi::ComputeKernelResource::Buffer(
                                  *m_impl->narrow_detector->GetResultBuffers().contact_point_b
                              )},
                             {"CollisionCount",
                              Rhi::ComputeKernelResource::Buffer(
                                  *m_impl->narrow_detector->GetResultBuffers().collision_count
                              )},
                             {"ShapeBoundRigidBody", Rhi::ComputeKernelResource::Buffer(*g.shape_bound_rigid_body)},
                             {"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_alive)},
                             {"RigidBodyCenterPosition",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_center_world_position)},
                             {"RigidBodyCenterRotation",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_center_world_rotation)},
                             {"RigidBodyMass", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_mass)},
                             {"RigidBodyInverseInertia",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_inverse_inertia)},
                             {"RigidBodyIsKinematic", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_is_kinematic)},
                             {"ShapeLocalPosition", Rhi::ComputeKernelResource::Buffer(*g.shape_local_position)},
                             {"ShapeLocalRotation", Rhi::ComputeKernelResource::Buffer(*g.shape_local_rotation)},
                             {"ContactLagrange", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_lagrange)},
                             {"Values", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_values)}},
                            contact_pt_wg,
                            1,
                            1,
                            push
                        );
                    }
                    barrier();
                    m_impl->RecordReduce(
                        cb,
                        contact_sorted,
                        *m_impl->gpu_contact_values,
                        *m_impl->gpu_contact_reduce_scratch,
                        *m_impl->gpu_contact_out_pos,
                        *m_impl->gpu_contact_entry_count,
                        contact_cap,
                        body_count
                    );
                    barrier();

                    // Hinge scatter + reduce.
                    dispatch_clear_values(
                        *m_impl->gpu_hinge_values, *m_impl->gpu_hinge_entry_count, hinge_slots, hinge_values_wg
                    );
                    barrier();
                    if (gpu.hinge_joint_count > 0u) {
                        const AccumJointPush push{m_impl->push_gravity_dt, gpu.hinge_joint_count, hinge_slots};
                        m_impl->accum_hinge_kernel->Dispatch(
                            cb,
                            {{"HingeJoints", Rhi::ComputeKernelResource::Buffer(*gpu.gpu_hinge_joints)},
                             {"HingeAxisLagrange",
                              Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_hinge_axis_lagrange)},
                             {"HingeAnchorLagrange",
                              Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_hinge_anchor_lagrange)},
                             {"HingeJointAlive", Rhi::ComputeKernelResource::Buffer(*gpu.gpu_hinge_joint_alive)},
                             {"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_alive)},
                             {"RigidBodyCenterPosition",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_center_world_position)},
                             {"RigidBodyCenterRotation",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_center_world_rotation)},
                             {"RigidBodyMass", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_mass)},
                             {"RigidBodyInverseInertia",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_inverse_inertia)},
                             {"RigidBodyIsKinematic", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_is_kinematic)},
                             {"Values", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_hinge_values)}},
                            hinge_wg,
                            1,
                            1,
                            push
                        );
                    }
                    barrier();
                    m_impl->RecordReduce(
                        cb,
                        hinge_sorted,
                        *m_impl->gpu_hinge_values,
                        *m_impl->gpu_hinge_reduce_scratch,
                        *m_impl->gpu_hinge_out,
                        *m_impl->gpu_hinge_entry_count,
                        hinge_slots,
                        body_count
                    );
                    barrier();

                    // Fixed scatter + reduce.
                    dispatch_clear_values(
                        *m_impl->gpu_fixed_values, *m_impl->gpu_fixed_entry_count, fixed_slots, fixed_values_wg
                    );
                    barrier();
                    if (gpu.fixed_joint_count > 0u) {
                        const AccumJointPush push{m_impl->push_gravity_dt, gpu.fixed_joint_count, fixed_slots};
                        m_impl->accum_fixed_kernel->Dispatch(
                            cb,
                            {{"FixedJoints", Rhi::ComputeKernelResource::Buffer(*gpu.gpu_fixed_joints)},
                             {"FixedRotationLagrange",
                              Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_fixed_rotation_lagrange)},
                             {"FixedPositionLagrange",
                              Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_fixed_position_lagrange)},
                             {"FixedJointAlive", Rhi::ComputeKernelResource::Buffer(*gpu.gpu_fixed_joint_alive)},
                             {"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_alive)},
                             {"RigidBodyCenterPosition",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_center_world_position)},
                             {"RigidBodyCenterRotation",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_center_world_rotation)},
                             {"RigidBodyMass", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_mass)},
                             {"RigidBodyInverseInertia",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_inverse_inertia)},
                             {"RigidBodyIsKinematic", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_is_kinematic)},
                             {"Values", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_fixed_values)}},
                            fixed_wg,
                            1,
                            1,
                            push
                        );
                    }
                    barrier();
                    m_impl->RecordReduce(
                        cb,
                        fixed_sorted,
                        *m_impl->gpu_fixed_values,
                        *m_impl->gpu_fixed_reduce_scratch,
                        *m_impl->gpu_fixed_out,
                        *m_impl->gpu_fixed_entry_count,
                        fixed_slots,
                        body_count
                    );
                    barrier();

                    // Merged position apply.
                    {
                        const BodyCountPush push{body_count};
                        m_impl->apply_pos_kernel->Dispatch(
                            cb,
                            {{"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_alive)},
                             {"RigidBodyCenterPosition",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_center_world_position)},
                             {"RigidBodyCenterRotation",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_center_world_rotation)},
                             {"RigidBodyIsKinematic", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_is_kinematic)},
                             {"ContactOut", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_out_pos)},
                             {"HingeOut", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_hinge_out)},
                             {"FixedOut", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_fixed_out)}},
                            body_wg,
                            1,
                            1,
                            push
                        );
                    }
                }

                // ====== PostPosition: update velocities from pose ======
                barrier();
                {
                    m_impl->update_vel_kernel->Dispatch(
                        cb,
                        {{"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_alive)},
                         {"RigidBodyCenterPosition",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_center_world_position)},
                         {"RigidBodyCenterRotation",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_center_world_rotation)},
                         {"RigidBodyLinearVelocity",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_linear_velocity)},
                         {"RigidBodyAngularVelocity",
                          Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_angular_velocity)},
                         {"RigidBodyIsKinematic", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_is_kinematic)},
                         {"SubstepStartPosition",
                          Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_substep_start_position)},
                         {"SubstepStartOrientation",
                          Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_substep_start_orientation)}},
                        body_wg,
                        1,
                        1,
                        m_impl->push_gravity_dt
                    );
                }

                // ====== Velocity iterations (reuse contact permutation) ======
                for (uint32_t iter = 0; iter < vel_iters; ++iter) {
                    barrier();
                    // Same value buffer as the position phase.  That phase has
                    // finished (its values were consumed by the last apply), and
                    // this clear erases them before the velocity scatter, so no
                    // position-phase value can reach the velocity reduction.
                    dispatch_clear_values(
                        *m_impl->gpu_contact_values, *m_impl->gpu_contact_entry_count, contact_cap, contact_values_wg
                    );
                    barrier();
                    {
                        const auto g = m_bound_scene->GetGpuBuffers();
                        const AccumVelocityPush push{m_impl->push_gravity_dt, contact_cap};
                        m_impl->accum_vel_kernel->Dispatch(
                            cb,
                            {{"CollisionIds",
                              Rhi::ComputeKernelResource::Buffer(
                                  *m_impl->narrow_detector->GetResultBuffers().collision_ids
                              )},
                             {"CollisionNormals",
                              Rhi::ComputeKernelResource::Buffer(
                                  *m_impl->narrow_detector->GetResultBuffers().collision_normals
                              )},
                             {"ContactPointA",
                              Rhi::ComputeKernelResource::Buffer(
                                  *m_impl->narrow_detector->GetResultBuffers().contact_point_a
                              )},
                             {"ContactPointB",
                              Rhi::ComputeKernelResource::Buffer(
                                  *m_impl->narrow_detector->GetResultBuffers().contact_point_b
                              )},
                             {"CollisionCount",
                              Rhi::ComputeKernelResource::Buffer(
                                  *m_impl->narrow_detector->GetResultBuffers().collision_count
                              )},
                             {"ShapeBoundRigidBody", Rhi::ComputeKernelResource::Buffer(*g.shape_bound_rigid_body)},
                             {"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_alive)},
                             {"RigidBodyCenterRotation",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_center_world_rotation)},
                             {"RigidBodyLinearVelocity",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_linear_velocity)},
                             {"RigidBodyAngularVelocity",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_angular_velocity)},
                             {"RigidBodyMass", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_mass)},
                             {"RigidBodyInverseInertia",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_inverse_inertia)},
                             {"RigidBodyDynamicFriction",
                              Rhi::ComputeKernelResource::Buffer(*g.rigid_body_dynamic_friction)},
                             {"RigidBodyRestitution", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_restitution)},
                             {"RigidBodyIsKinematic", Rhi::ComputeKernelResource::Buffer(*g.rigid_body_is_kinematic)},
                             {"PreContactLinearVelocity",
                              Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_pre_contact_linear_vel)},
                             {"PreContactAngularVelocity",
                              Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_pre_contact_angular_vel)},
                             {"ShapeLocalPosition", Rhi::ComputeKernelResource::Buffer(*g.shape_local_position)},
                             {"ShapeLocalRotation", Rhi::ComputeKernelResource::Buffer(*g.shape_local_rotation)},
                             {"ContactLagrange", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_lagrange)},
                             {"Values", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_values)}},
                            contact_pt_wg,
                            1,
                            1,
                            push
                        );
                    }
                    barrier();
                    m_impl->RecordReduce(
                        cb,
                        contact_sorted,
                        *m_impl->gpu_contact_values,
                        *m_impl->gpu_contact_reduce_scratch,
                        *m_impl->gpu_contact_out_vel,
                        *m_impl->gpu_contact_entry_count,
                        contact_cap,
                        body_count
                    );
                    barrier();
                    {
                        const BodyCountPush push{body_count};
                        m_impl->apply_vel_kernel->Dispatch(
                            cb,
                            {{"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_alive)},
                             {"RigidBodyIsKinematic", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_is_kinematic)},
                             {"RigidBodyLinearVelocity",
                              Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_linear_velocity)},
                             {"RigidBodyAngularVelocity",
                              Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_angular_velocity)},
                             {"VelOut", Rhi::ComputeKernelResource::Buffer(*m_impl->gpu_contact_out_vel)}},
                            body_wg,
                            1,
                            1,
                            push
                        );
                    }
                }
            }
        }
    }

    void XpbdGpuSolver::GPUCalcModelMatrices(vk::CommandBuffer cb, Rhi::ComputeBuffer &target) {
        const auto gpu = m_bound_scene->GetGpuBuffers();
        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) return;
        if (gpu.rigid_body_center_world_position == nullptr || gpu.rigid_body_center_world_rotation == nullptr) return;

        const uint32_t body_count = gpu.rigid_body_slot_count;
        const uint32_t target_capacity = static_cast<uint32_t>(target.GetSize() / sizeof(glm::mat4));

        // The caller is responsible for the target's capacity. A shortfall is a
        // programming error: report it in debug builds and clamp in release
        // rather than writing out of bounds.
        assert(
            body_count <= target_capacity
            && "GPUCalcModelMatrices target is smaller than the scene's rigid body slot count"
        );
        const uint32_t write_count = std::min(body_count, target_capacity);
        if (write_count == 0u) return;

        m_impl->EnsureShadersLoaded();

        // An explicit production is not part of the step, so it records the
        // barrier that makes the poses it reads visible.
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        m_impl->model_matrix_kernel->Dispatch(
            cb,
            {{"RigidBodyAlive", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_alive)},
             {"RigidBodyCenterPosition", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_center_world_position)},
             {"RigidBodyCenterRotation", Rhi::ComputeKernelResource::Buffer(*gpu.rigid_body_center_world_rotation)},
             {"ModelMatrices", Rhi::ComputeKernelResource::Buffer(target)}},
            (write_count + 63u) / 64u,
            1,
            1
        );
    }
} // namespace Engine
