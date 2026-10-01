#include "XPBDGpuSolver.h"

#include <vulkan/vulkan.hpp>

#include <Physics/Collision/ConvexCollisionDetector.h>
#include <Physics/Collision/SpatialHashBroadDetector.h>
#include <Physics/PhysicsDispatch.h>
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
    constexpr uint32_t kNumChannels = 7u; // Δlin3 + Δang3 + flag

    // Hinge/fixed joints own 4 entry slots each: 2 bodies x 2 scalar constraints.
    constexpr uint32_t kJointSlotsPerJoint = 4u;

    // Push-constant layouts matching the per-shader blocks (std430).  A block
    // whose only field is a uint is passed as that uint: the reflected block is
    // the same four bytes, so naming it with a struct buys nothing.
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
        uint32_t body_count;
    };
    static_assert(sizeof(JointCountPush) == 12);

    struct ContactEntryPush { // entries/contact_entries.comp
        uint32_t entry_capacity;
        uint32_t body_count;
        uint32_t shape_slot_count;
    };
    static_assert(sizeof(ContactEntryPush) == 12);

    struct ClearEntryValuesPush { // clear_entry_values.comp
        uint32_t entry_capacity;  // channel stride
        uint32_t num_channels;
    };
    static_assert(sizeof(ClearEntryValuesPush) == 8);

    struct ClearEntryValuesPushCount { // clear_entry_values_push.comp
        uint32_t entry_capacity;       // channel stride
        uint32_t num_channels;
        uint32_t entry_count; // CPU-known count
    };
    static_assert(sizeof(ClearEntryValuesPushCount) == 12);
} // namespace

namespace Engine {

    struct XpbdGpuSolver::Impl {
        Rhi::DeviceContext &device_context;

        bool shaders_loaded = false;
        XpbdConfig config{};
        uint32_t max_contact_point = 0u;

        /**
         * @brief The geometry one step runs with, derived once by `PrepareStep`.
         *
         * `*_count` is a group's exact entry count — how many leading slots carry
         * real entries — and `*_slots` is its entry capacity, which keeps a floor
         * of one joint's worth so an empty joint list still has a fully written
         * entry list.
         */
        struct StepGeometry {
            uint32_t body_count = 0u;
            uint32_t shape_count = 0u;
            uint32_t contact_cap = 0u; // 2 * max_contact_point
            uint32_t hinge_joints = 0u;
            uint32_t fixed_joints = 0u;
            uint32_t hinge_count = 0u;
            uint32_t fixed_count = 0u;
            uint32_t hinge_slots = 0u;
            uint32_t fixed_slots = 0u;
        };

        StepGeometry step{};

        std::unique_ptr<SpatialHashBroadDetector> broad_detector{};
        std::unique_ptr<ConvexCollisionDetector> narrow_detector{};

        // ---- Compute kernels (device-owned, acquired by PrepareStep) ----
        Rhi::ComputeKernel *clear_int_kernel = nullptr;
        Rhi::ComputeKernel *clear_values_kernel = nullptr;
        Rhi::ComputeKernel *clear_values_push_kernel = nullptr;
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
        // One sort and one reduction serve every constraint type
        std::unique_ptr<RadixSort> radix_sort{};
        std::unique_ptr<SumByKey> sum_by_key{};

        /**
         * @brief One constraint type's entry list and its reduction buffers.
         *
         * The entry list is a struct-of-arrays record: a key array plus a payload
         * array holding each entry's own slot id.  Both are plain data as far as
         * the algorithms are concerned — the entry pass writes them, the sort
         * sorts them in place and the reduction's level-0 gather reads them — so
         * the working storage the sort and the reduction need is theirs, and
         * nothing here has to size it or know where the sorted records landed.
         * `values` holds the per-entry contribution values, `out` the per-body
         * reduction result.
         *
         * `entry_count` is device-local: the group's entry pass publishes the
         * substep's entry count on the GPU, the radix sort's count guard reads it
         * there, and the reduction takes it either from that buffer or as a
         * push-constant value, depending on what the caller knows.
         */
        struct ReduceGroup {
            std::unique_ptr<Rhi::ComputeBuffer> keys{};
            std::unique_ptr<Rhi::ComputeBuffer> payload{};
            std::unique_ptr<Rhi::ComputeBuffer> entry_count{};
            std::unique_ptr<Rhi::ComputeBuffer> values{};
            std::unique_ptr<Rhi::ComputeBuffer> out{};
        };

        ReduceGroup contact{};
        ReduceGroup hinge{};
        ReduceGroup fixed{};

        // The contact group reduces twice per substep — once for the position
        // phase and once for the velocity phase — and the two phases write
        // different per-body outputs.  The joint groups reduce once.
        std::unique_ptr<Rhi::ComputeBuffer> contact_out_vel{};

        glm::vec4 push_gravity_dt{0.0f, 0.0f, -9.81f, 0.0f};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        /// @brief Grow a solver buffer to at least `bytes`, following the shared
        /// capacity rule: created on first use, geometric and grow-only after.
        void EnsureBuffer(std::unique_ptr<Rhi::ComputeBuffer> &buf, size_t bytes, const char *name) {
            Rhi::EnsureComputeBuffer(buf, device_context.GetAllocatorState(), bytes, false, name);
        }

        /// @brief Size one group's buffers for the geometry this step reports.
        ///
        /// The set is fixed per group: each buffer covers one geometry the step
        /// reports and grows geometrically from there.
        void EnsureReduceGroup(ReduceGroup &g, uint32_t capacity, uint32_t body_count) {
            const size_t cap_bytes = static_cast<size_t>(capacity) * sizeof(uint32_t);
            EnsureBuffer(g.keys, cap_bytes, "XPBD ReduceKeys");
            EnsureBuffer(g.payload, cap_bytes, "XPBD ReducePayload");
            Rhi::EnsureComputeBuffer(
                g.entry_count, device_context.GetAllocatorState(), sizeof(uint32_t), false, "XPBD ReduceEntryCount"
            );
            EnsureBuffer(g.values, static_cast<size_t>(kNumChannels) * cap_bytes, "XPBD ReduceValues");
            EnsureBuffer(g.out, static_cast<size_t>(kNumChannels) * body_count * sizeof(uint32_t), "XPBD ReduceOut");
        }

        /// @brief Sort one group's entry list over the whole capacity.
        ///
        /// The key domain is host-known and tight: every key is a body index
        /// (`< body_count`) or the invalid-slot sentinel (`== body_count`), so
        /// `body_count` bounds all of them and keeps the entry count a prefix of
        /// the sorted order.  The sort leaves the result in the group's own key
        /// and payload arrays whichever pass count it derived, so the reduction
        /// binds those directly.
        void RecordSort(vk::CommandBuffer cb, ReduceGroup &g, uint32_t capacity, uint32_t body_count) {
            assert(body_count > 0u && "the entry shaders use body_count as their invalid-slot key");
            const RadixSortBuffers buffers{
                .keys = g.keys.get(),
                .payload = g.payload.get(),
                .count = g.entry_count.get(),
            };
            radix_sort->Record(cb, buffers, capacity, body_count);
        }

        /// @brief Reduce one group's contribution values into `out`.
        void RecordReduce(
            vk::CommandBuffer cb,
            const ReduceGroup &g,
            Rhi::ComputeBuffer &out,
            SumByKey::EntryCountSource entry_count,
            uint32_t capacity,
            uint32_t body_count
        ) {
            sum_by_key->Record(
                cb, *g.keys, *g.payload, *g.values, out, entry_count, capacity, kNumChannels, body_count
            );
        }

        void EnsureShadersLoaded() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            auto load = [this](const char *path, const char *name) {
                return &LoadPhysicsKernel(device_context, path, name);
            };

            clear_int_kernel = load("solver/XPBDSolver/clear_int_buffer.comp.spv", "XPBD Clear Int");
            clear_values_kernel = load("solver/XPBDSolver/clear_entry_values.comp.spv", "XPBD ClearEntryValues");
            clear_values_push_kernel =
                load("solver/XPBDSolver/clear_entry_values_push.comp.spv", "XPBD ClearEntryValuesPush");

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

    bool XpbdGpuSolver::PrepareStep() {
        const auto gpu = m_bound_scene->GetGpuBuffers();
        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_slot_count == 0u) return false;

        m_impl->EnsureShadersLoaded();

        // Every capacity and count this step needs is derived here, once, so
        // GPUStep only derives dispatch geometry from these values.
        Impl::StepGeometry &s = m_impl->step;
        s.body_count = gpu.rigid_body_slot_count;
        s.shape_count = gpu.shape_slot_count;
        const uint32_t all_pairs = s.shape_count > 1u ? (s.shape_count * (s.shape_count - 1u)) / 2u : 0u;
        m_impl->max_contact_point = std::max(1u, std::min(all_pairs * 5u, m_impl->config.max_contact_points));
        s.contact_cap = 2u * m_impl->max_contact_point;

        // A joint count of zero still owns one joint's worth of slots so its entry
        // pass writes a whole entry list, while the group's entry *count* is zero
        // and its clear and reduction are no-ops.
        s.hinge_joints = gpu.hinge_joint_count;
        s.fixed_joints = gpu.fixed_joint_count;
        s.hinge_count = kJointSlotsPerJoint * s.hinge_joints;
        s.fixed_count = kJointSlotsPerJoint * s.fixed_joints;
        s.hinge_slots = kJointSlotsPerJoint * std::max(1u, s.hinge_joints);
        s.fixed_slots = kJointSlotsPerJoint * std::max(1u, s.fixed_joints);

        const uint32_t body_count = s.body_count;

        m_impl->EnsureBuffer(
            m_impl->gpu_contact_lagrange,
            static_cast<size_t>(m_impl->max_contact_point) * sizeof(float),
            "XPBD ContactLagrange"
        );
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

        m_impl->EnsureReduceGroup(m_impl->contact, s.contact_cap, body_count);
        m_impl->EnsureBuffer(
            m_impl->contact_out_vel,
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
        const uint32_t hinge_slots = s.hinge_slots;
        const uint32_t fixed_slots = s.fixed_slots;

        m_impl->EnsureReduceGroup(m_impl->hinge, hinge_slots, body_count);
        m_impl->EnsureBuffer(
            m_impl->gpu_hinge_axis_lagrange,
            static_cast<size_t>(std::max(1u, s.hinge_joints)) * sizeof(float),
            "XPBD HingeAxisLagrange"
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_hinge_anchor_lagrange,
            static_cast<size_t>(std::max(1u, s.hinge_joints)) * sizeof(float),
            "XPBD HingeAnchorLagrange"
        );
        // The hinge group's entry count is published by its entry pass on the GPU
        // (the radix sort's count guard reads it) and travels as a push-constant
        // value in the reduction and the counted clear, so no host write exists.

        m_impl->EnsureReduceGroup(m_impl->fixed, fixed_slots, body_count);
        m_impl->EnsureBuffer(
            m_impl->gpu_fixed_rotation_lagrange,
            static_cast<size_t>(std::max(1u, s.fixed_joints)) * sizeof(float),
            "XPBD FixedRotLagrange"
        );
        m_impl->EnsureBuffer(
            m_impl->gpu_fixed_position_lagrange,
            static_cast<size_t>(std::max(1u, s.fixed_joints)) * sizeof(float),
            "XPBD FixedPosLagrange"
        );
        // As for the hinge group, the fixed entry count never reaches a host write.

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
            m_impl->broad_detector->BindToScene(
                *m_bound_scene,
                grid_config,
                m_impl->config.fallback_all_pairs_threshold,
                m_impl->config.max_global_shape_count
            );

            if (!m_impl->narrow_detector) {
                m_impl->narrow_detector = std::make_unique<ConvexCollisionDetector>(m_impl->device_context);
            }
            // The narrow detector takes its pair buffers from the broad detector's
            // live output at its own preparation time, so the solver only names the
            // source and the contact budget.
            m_impl->narrow_detector->BindToScene(
                *m_bound_scene,
                *m_impl->broad_detector,
                m_impl->config.max_contact_points,
                m_impl->config.contact_margin
            );
        }

        return true;
    }

    void XpbdGpuSolver::GPUStep(vk::CommandBuffer cb) {
        // Preparation happens here, at the record site, and completes before this
        // call's first dispatch: kernels are acquired, buffers are sized for the
        // geometry the scene currently reports, push-constant values are derived
        // and the detectors are bound.
        if (!PrepareStep()) return;

        const auto gpu = m_bound_scene->GetGpuBuffers();
        const Impl::StepGeometry &s = m_impl->step;

        const uint32_t body_count = s.body_count;
        const uint32_t shape_count = s.shape_count;
        const uint32_t body_wg = (body_count + 63u) / 64u;
        const uint32_t shape_wg = (shape_count + 63u) / 64u;

        auto barrier = [&cb]() { DispatchBarrier(cb); };

        // A snapshot dispatch copies one scene column into a solver-owned one, so
        // its two bindings and its geometry are the same at every call site.
        auto snapshot = [this, &cb, body_wg, body_count](const Rhi::ComputeBuffer &src, Rhi::ComputeBuffer &dst) {
            m_impl->snapshot_kernel->Dispatch(cb, {{"SrcBuffer", src}, {"DstBuffer", dst}}, body_wg, 1, 1, body_count);
        };

        // The clear writes one element per element of the range, so its geometry
        // follows the range it is given.
        auto dispatch_clear = [this, &cb](Rhi::ComputeBuffer &tgt, uint32_t elem_count) {
            m_impl->clear_int_kernel->Dispatch(cb, {{"Target", tgt}}, (elem_count + 63u) / 64u, 1, 1, elem_count);
        };

        // Count-bounded clear of a group's per-iteration value buffer: only the
        // slots below that substep's entry count are written.  The dispatch
        // geometry follows the group's entry *capacity*, because the shader bounds
        // its writes by the count it is given and the count of the contact group is
        // only known on the GPU.
        auto dispatch_clear_values =
            [this, &cb](Rhi::ComputeBuffer &values, Rhi::ComputeBuffer &entry_count, uint32_t capacity) {
                const ClearEntryValuesPush push{capacity, kNumChannels};
                const uint32_t wg = (kNumChannels * capacity + 63u) / 64u;
                m_impl->clear_values_kernel->Dispatch(
                    cb, {{"Values", values}, {"EntryCount", entry_count}}, wg, 1, 1, push
                );
            };

        // Count-bounded clear for a joint group, whose entry count the host knows.
        // The count bounds a *data* extent only, so an empty group records no clear
        // at all; otherwise the dispatch geometry follows the count itself.
        auto dispatch_clear_values_push = [this, &cb](Rhi::ComputeBuffer &values, uint32_t capacity, uint32_t count) {
            if (count == 0u) return;
            const ClearEntryValuesPushCount push{capacity, kNumChannels, count};
            m_impl->clear_values_push_kernel->Dispatch(
                cb, {{"Values", values}}, (kNumChannels * count + 63u) / 64u, 1, 1, push
            );
        };

        if (m_bound_scene->IsSimulationEnabled()) {
            const uint32_t substep_count = std::max(1u, m_impl->config.num_substep_perstep);
            const uint32_t pos_iters = std::max(1u, m_impl->config.num_iter_persubstep);
            const uint32_t vel_iters = std::max(1u, m_impl->config.num_velocity_iters);

            // Everything capacity-dependent was sized by PrepareStep above; this
            // point in the call only derives dispatch geometry from the geometry it
            // derived.
            const uint32_t contact_cap = s.contact_cap;
            const uint32_t contact_pt_wg = (m_impl->max_contact_point + 63u) / 64u;
            const uint32_t hinge_count = s.hinge_count;
            const uint32_t fixed_count = s.fixed_count;
            const uint32_t hinge_slots = s.hinge_slots;
            const uint32_t fixed_slots = s.fixed_slots;
            const uint32_t hinge_entry_wg = (std::max(1u, s.hinge_joints) + 63u) / 64u;
            const uint32_t fixed_entry_wg = (std::max(1u, s.fixed_joints) + 63u) / 64u;
            const uint32_t hinge_wg = (s.hinge_joints + 63u) / 64u;
            const uint32_t fixed_wg = (s.fixed_joints + 63u) / 64u;
            const uint32_t out_elems = kNumChannels * body_count;

            for (uint32_t ss = 0; ss < substep_count; ++ss) {
                // ====== PreCollision ======
                barrier();
                snapshot(*gpu.rigid_body_center_world_position, *m_impl->gpu_substep_start_position);
                barrier();
                snapshot(*gpu.rigid_body_center_world_rotation, *m_impl->gpu_substep_start_orientation);
                barrier();

                {
                    m_impl->integrate_kernel->Dispatch(
                        cb,
                        {{"RigidBodyAlive", *gpu.rigid_body_alive},
                         {"RigidBodyCenterPosition", *gpu.rigid_body_center_world_position},
                         {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation},
                         {"RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity},
                         {"RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity},
                         {"RigidBodyMass", *gpu.rigid_body_mass},
                         {"RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia},
                         {"RigidBodyInertia", *gpu.rigid_body_inertia},
                         {"RigidBodyExternalForce", *gpu.rigid_body_external_force},
                         {"RigidBodyExternalTorque", *gpu.rigid_body_external_torque},
                         {"RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic}},
                        body_wg,
                        1,
                        1,
                        m_impl->push_gravity_dt
                    );
                }
                barrier();
                snapshot(*gpu.rigid_body_linear_velocity, *m_impl->gpu_pre_contact_linear_vel);
                barrier();
                snapshot(*gpu.rigid_body_angular_velocity, *m_impl->gpu_pre_contact_angular_vel);
                barrier();

                if (shape_count > 1u && gpu.shape_world_position != nullptr) {
                    m_impl->update_shape_world_pose_kernel->Dispatch(
                        cb,
                        {{"ShapeAlive", *gpu.shape_alive},
                         {"ShapeBoundRigidBody", *gpu.shape_bound_rigid_body},
                         {"ShapeLocalPosition", *gpu.shape_local_position},
                         {"ShapeLocalRotation", *gpu.shape_local_rotation},
                         {"RigidBodyCenterPosition", *gpu.rigid_body_center_world_position},
                         {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation},
                         {"ShapeWorldPosition", *gpu.shape_world_position},
                         {"ShapeWorldRotation", *gpu.shape_world_rotation}},
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

                // The narrow detector's live result buffers, taken after its own
                // preparation above, so the rest of the substep binds the buffers
                // this substep actually wrote.
                const CollisionResultBuffers narrow = m_impl->narrow_detector->GetResultBuffers();

                // ====== Entry lists: build → sort (all three types) ======
                // The entry passes are dispatched over the entry capacity and write
                // every slot on every dispatch (a key — body index or the
                // `body_count` invalid-slot key — plus the slot's own id as its
                // payload), so the sorted key array covers the whole capacity every
                // substep.  The sort owns its ping-pong arrays and leaves the result
                // in the group's own arrays whatever pass count it derived, so the
                // reduction binds those directly.
                {
                    const ContactEntryPush push{contact_cap, body_count, shape_count};
                    m_impl->contact_entries_kernel->Dispatch(
                        cb,
                        {{"CollisionIds", *narrow.collision_ids},
                         {"CollisionCount", *narrow.collision_count},
                         {"ShapeBoundRigidBody", *gpu.shape_bound_rigid_body},
                         {"EntryKeys", *m_impl->contact.keys},
                         {"EntryPayload", *m_impl->contact.payload},
                         // The entry pass derives and publishes the substep's entry
                         // count from the collision count it reads here.
                         {"EntryCount", *m_impl->contact.entry_count}},
                        contact_pt_wg,
                        1,
                        1,
                        push
                    );
                }
                barrier();
                m_impl->RecordSort(cb, m_impl->contact, contact_cap, body_count);
                barrier();

                {
                    const JointCountPush push{s.hinge_joints, hinge_slots, body_count};
                    m_impl->hinge_entries_kernel->Dispatch(
                        cb,
                        {{"HingeJoints", *gpu.gpu_hinge_joints},
                         {"EntryKeys", *m_impl->hinge.keys},
                         {"EntryPayload", *m_impl->hinge.payload},
                         // The entry pass publishes the group's count for the radix
                         // sort's count guard; the reduction takes the push value.
                         {"EntryCount", *m_impl->hinge.entry_count}},
                        hinge_entry_wg,
                        1,
                        1,
                        push
                    );
                }
                barrier();
                m_impl->RecordSort(cb, m_impl->hinge, hinge_slots, body_count);
                barrier();

                {
                    const JointCountPush push{s.fixed_joints, fixed_slots, body_count};
                    m_impl->fixed_entries_kernel->Dispatch(
                        cb,
                        {{"FixedJoints", *gpu.gpu_fixed_joints},
                         {"EntryKeys", *m_impl->fixed.keys},
                         {"EntryPayload", *m_impl->fixed.payload},
                         // The entry pass publishes the group's count for the radix
                         // sort's count guard; the reduction takes the push value.
                         {"EntryCount", *m_impl->fixed.entry_count}},
                        fixed_entry_wg,
                        1,
                        1,
                        push
                    );
                }
                barrier();
                m_impl->RecordSort(cb, m_impl->fixed, fixed_slots, body_count);
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
                dispatch_clear(*m_impl->gpu_contact_lagrange, m_impl->max_contact_point);
                barrier();
                dispatch_clear(*m_impl->contact.out, out_elems);
                barrier();
                dispatch_clear(*m_impl->contact_out_vel, out_elems);
                barrier();
                dispatch_clear(*m_impl->hinge.out, out_elems);
                barrier();
                dispatch_clear(*m_impl->fixed.out, out_elems);
                barrier();
                {
                    m_impl->clear_hinge_kernel->Dispatch(
                        cb,
                        {{"HingeAxisLagrange", *m_impl->gpu_hinge_axis_lagrange},
                         {"HingeAnchorLagrange", *m_impl->gpu_hinge_anchor_lagrange}},
                        std::max(1u, (s.hinge_joints + 255u) / 256u),
                        1,
                        1,
                        s.hinge_joints
                    );
                }
                barrier();
                {
                    m_impl->clear_fixed_kernel->Dispatch(
                        cb,
                        {{"FixedRotationLagrange", *m_impl->gpu_fixed_rotation_lagrange},
                         {"FixedPositionLagrange", *m_impl->gpu_fixed_position_lagrange}},
                        std::max(1u, (s.fixed_joints + 255u) / 256u),
                        1,
                        1,
                        s.fixed_joints
                    );
                }
                barrier();

                // ====== Position Iterations ======
                for (uint32_t iter = 0; iter < pos_iters; ++iter) {
                    barrier();

                    // Contact scatter (position value buffer) — cleared first, so a
                    // non-contributing contact leaves zeroes with flag 0.  The clear
                    // is count-bounded: only slots below the entry count can be
                    // written this iteration, and only those are read back.
                    dispatch_clear_values(*m_impl->contact.values, *m_impl->contact.entry_count, contact_cap);
                    barrier();
                    m_impl->accum_pos_kernel->Dispatch(
                        cb,
                        {{"CollisionIds", *narrow.collision_ids},
                         {"CollisionNormals", *narrow.collision_normals},
                         {"ContactPointA", *narrow.contact_point_a},
                         {"ContactPointB", *narrow.contact_point_b},
                         {"CollisionCount", *narrow.collision_count},
                         {"ShapeBoundRigidBody", *gpu.shape_bound_rigid_body},
                         {"RigidBodyAlive", *gpu.rigid_body_alive},
                         {"RigidBodyCenterPosition", *gpu.rigid_body_center_world_position},
                         {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation},
                         {"RigidBodyMass", *gpu.rigid_body_mass},
                         {"RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia},
                         {"RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic},
                         {"ShapeLocalPosition", *gpu.shape_local_position},
                         {"ShapeLocalRotation", *gpu.shape_local_rotation},
                         {"ContactLagrange", *m_impl->gpu_contact_lagrange},
                         {"Values", *m_impl->contact.values}},
                        contact_pt_wg,
                        1,
                        1,
                        contact_cap
                    );
                    barrier();
                    m_impl->RecordReduce(
                        cb,
                        m_impl->contact,
                        *m_impl->contact.out,
                        &*m_impl->contact.entry_count,
                        contact_cap,
                        body_count
                    );
                    barrier();

                    // Hinge scatter + reduce.
                    dispatch_clear_values_push(*m_impl->hinge.values, hinge_slots, hinge_count);
                    barrier();
                    if (s.hinge_joints > 0u) {
                        const AccumJointPush push{m_impl->push_gravity_dt, s.hinge_joints, hinge_slots};
                        m_impl->accum_hinge_kernel->Dispatch(
                            cb,
                            {{"HingeJoints", *gpu.gpu_hinge_joints},
                             {"HingeAxisLagrange", *m_impl->gpu_hinge_axis_lagrange},
                             {"HingeAnchorLagrange", *m_impl->gpu_hinge_anchor_lagrange},
                             {"HingeJointAlive", *gpu.gpu_hinge_joint_alive},
                             {"RigidBodyAlive", *gpu.rigid_body_alive},
                             {"RigidBodyCenterPosition", *gpu.rigid_body_center_world_position},
                             {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation},
                             {"RigidBodyMass", *gpu.rigid_body_mass},
                             {"RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia},
                             {"RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic},
                             {"Values", *m_impl->hinge.values}},
                            hinge_wg,
                            1,
                            1,
                            push
                        );
                    }
                    barrier();
                    m_impl->RecordReduce(cb, m_impl->hinge, *m_impl->hinge.out, hinge_count, hinge_slots, body_count);
                    barrier();

                    // Fixed scatter + reduce.
                    dispatch_clear_values_push(*m_impl->fixed.values, fixed_slots, fixed_count);
                    barrier();
                    if (s.fixed_joints > 0u) {
                        const AccumJointPush push{m_impl->push_gravity_dt, s.fixed_joints, fixed_slots};
                        m_impl->accum_fixed_kernel->Dispatch(
                            cb,
                            {{"FixedJoints", *gpu.gpu_fixed_joints},
                             {"FixedRotationLagrange", *m_impl->gpu_fixed_rotation_lagrange},
                             {"FixedPositionLagrange", *m_impl->gpu_fixed_position_lagrange},
                             {"FixedJointAlive", *gpu.gpu_fixed_joint_alive},
                             {"RigidBodyAlive", *gpu.rigid_body_alive},
                             {"RigidBodyCenterPosition", *gpu.rigid_body_center_world_position},
                             {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation},
                             {"RigidBodyMass", *gpu.rigid_body_mass},
                             {"RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia},
                             {"RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic},
                             {"Values", *m_impl->fixed.values}},
                            fixed_wg,
                            1,
                            1,
                            push
                        );
                    }
                    barrier();
                    m_impl->RecordReduce(cb, m_impl->fixed, *m_impl->fixed.out, fixed_count, fixed_slots, body_count);
                    barrier();

                    // Merged position apply.
                    m_impl->apply_pos_kernel->Dispatch(
                        cb,
                        {{"RigidBodyAlive", *gpu.rigid_body_alive},
                         {"RigidBodyCenterPosition", *gpu.rigid_body_center_world_position},
                         {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation},
                         {"RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic},
                         {"ContactOut", *m_impl->contact.out},
                         {"HingeOut", *m_impl->hinge.out},
                         {"FixedOut", *m_impl->fixed.out}},
                        body_wg,
                        1,
                        1,
                        body_count
                    );
                }

                // ====== PostPosition: update velocities from pose ======
                barrier();
                m_impl->update_vel_kernel->Dispatch(
                    cb,
                    {{"RigidBodyAlive", *gpu.rigid_body_alive},
                     {"RigidBodyCenterPosition", *gpu.rigid_body_center_world_position},
                     {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation},
                     {"RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity},
                     {"RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity},
                     {"RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic},
                     {"SubstepStartPosition", *m_impl->gpu_substep_start_position},
                     {"SubstepStartOrientation", *m_impl->gpu_substep_start_orientation}},
                    body_wg,
                    1,
                    1,
                    m_impl->push_gravity_dt
                );

                // ====== Velocity iterations (reuse contact permutation) ======
                for (uint32_t iter = 0; iter < vel_iters; ++iter) {
                    barrier();
                    // Same value buffer as the position phase.  That phase has
                    // finished (its values were consumed by the last apply), and
                    // this clear erases them before the velocity scatter, so no
                    // position-phase value can reach the velocity reduction.
                    dispatch_clear_values(*m_impl->contact.values, *m_impl->contact.entry_count, contact_cap);
                    barrier();
                    {
                        const AccumVelocityPush push{m_impl->push_gravity_dt, contact_cap};
                        m_impl->accum_vel_kernel->Dispatch(
                            cb,
                            {{"CollisionIds", *narrow.collision_ids},
                             {"CollisionNormals", *narrow.collision_normals},
                             {"ContactPointA", *narrow.contact_point_a},
                             {"ContactPointB", *narrow.contact_point_b},
                             {"CollisionCount", *narrow.collision_count},
                             {"ShapeBoundRigidBody", *gpu.shape_bound_rigid_body},
                             {"RigidBodyAlive", *gpu.rigid_body_alive},
                             {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation},
                             {"RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity},
                             {"RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity},
                             {"RigidBodyMass", *gpu.rigid_body_mass},
                             {"RigidBodyInverseInertia", *gpu.rigid_body_inverse_inertia},
                             {"RigidBodyDynamicFriction", *gpu.rigid_body_dynamic_friction},
                             {"RigidBodyRestitution", *gpu.rigid_body_restitution},
                             {"RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic},
                             {"PreContactLinearVelocity", *m_impl->gpu_pre_contact_linear_vel},
                             {"PreContactAngularVelocity", *m_impl->gpu_pre_contact_angular_vel},
                             {"ShapeLocalPosition", *gpu.shape_local_position},
                             {"ShapeLocalRotation", *gpu.shape_local_rotation},
                             {"ContactLagrange", *m_impl->gpu_contact_lagrange},
                             {"Values", *m_impl->contact.values}},
                            contact_pt_wg,
                            1,
                            1,
                            push
                        );
                    }
                    barrier();
                    m_impl->RecordReduce(
                        cb,
                        m_impl->contact,
                        *m_impl->contact_out_vel,
                        &*m_impl->contact.entry_count,
                        contact_cap,
                        body_count
                    );
                    barrier();
                    m_impl->apply_vel_kernel->Dispatch(
                        cb,
                        {{"RigidBodyAlive", *gpu.rigid_body_alive},
                         {"RigidBodyIsKinematic", *gpu.rigid_body_is_kinematic},
                         {"RigidBodyLinearVelocity", *gpu.rigid_body_linear_velocity},
                         {"RigidBodyAngularVelocity", *gpu.rigid_body_angular_velocity},
                         {"VelOut", *m_impl->contact_out_vel}},
                        body_wg,
                        1,
                        1,
                        body_count
                    );
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
        DispatchBarrier(cb);

        m_impl->model_matrix_kernel->Dispatch(
            cb,
            {{"RigidBodyAlive", *gpu.rigid_body_alive},
             {"RigidBodyCenterPosition", *gpu.rigid_body_center_world_position},
             {"RigidBodyCenterRotation", *gpu.rigid_body_center_world_rotation},
             {"ModelMatrices", target}},
            (write_count + 63u) / 64u,
            1,
            1
        );
    }
} // namespace Engine
