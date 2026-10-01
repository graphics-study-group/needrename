#include "ConvexCollisionDetector.h"

#include <vulkan/vulkan.hpp>

#include <Physics/Collision/SpatialHashBroadDetector.h>
#include <Physics/PhysicsDispatch.h>
#include <Physics/PhysicsScene.h>
#include <Physics/PhysicsSpirvLoader.h>
#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeKernel.h>

#include <algorithm>
#include <cassert>
#include <vector>

namespace Engine {

    // Push-constant layout, matching the block in detect_collisions.comp (std430).
    struct DetectPushParams {
        float contact_margin;
        uint32_t shape_slot_count;
        uint32_t contact_budget;
    };
    static_assert(sizeof(DetectPushParams) == 12, "DetectPushParams must match shader push block");

    struct ConvexCollisionDetector::Impl {
        Rhi::DeviceContext &device_context;

        PhysicsScene *bound_scene = nullptr;
        const SpatialHashBroadDetector *broad_detector = nullptr;

        // The broad-phase output, refreshed from the detector's live result buffers
        // at every preparation rather than held from a previous step.
        const Rhi::ComputeBuffer *pair_buffer = nullptr;
        const Rhi::ComputeBuffer *pair_count_buffer = nullptr;

        uint32_t max_input_collision_pairs = 1;
        uint32_t max_output_collision_pairs = 1;
        uint32_t max_contact_points = 1;
        float contact_margin = 0.001f;
        uint32_t shape_slot_count = 0;

        bool shaders_loaded = false;
        /// True once the result buffers and kernels match the observed capacity.
        bool prepared = false;

        Rhi::ComputeKernel *clear_kernel = nullptr;
        Rhi::ComputeKernel *detect_kernel = nullptr;

        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_ids{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_normals{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_point_a{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_contact_point_b{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_count{};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        /// @brief Grow a detector buffer to at least `bytes`, following the shared
        /// capacity rule: created on first use, geometric and grow-only after.
        void EnsureBuffer(std::unique_ptr<Rhi::ComputeBuffer> &buf, size_t bytes, const char *name) {
            Rhi::EnsureComputeBuffer(buf, device_context.GetAllocatorState(), bytes, false, name);
        }

        void EnsureBuffers() {
            const size_t result_entries = std::max<uint32_t>(1u, max_output_collision_pairs);

            EnsureBuffer(gpu_collision_ids, result_entries * sizeof(glm::uvec2), "CollisionIds");
            EnsureBuffer(gpu_collision_normals, result_entries * sizeof(glm::vec4), "CollisionNormals");
            EnsureBuffer(gpu_contact_point_a, result_entries * sizeof(glm::vec4), "ContactPointA");
            EnsureBuffer(gpu_contact_point_b, result_entries * sizeof(glm::vec4), "ContactPointB");
            EnsureBuffer(gpu_collision_count, sizeof(uint32_t), "CollisionCount");
        }

        void EnsureKernels() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            // `clear_int_buffer.comp` is shared with the XPBD solver: both
            // components resolve to the same device-level kernel.
            clear_kernel = &LoadPhysicsKernel(
                device_context, "solver/XPBDSolver/clear_int_buffer.comp.spv", "ConvexDetect ClearCount"
            );
            detect_kernel = &LoadPhysicsKernel(
                device_context,
                "collision/ConvexCollisionDetector/detect_collisions.comp.spv",
                "Convex Collision Detection"
            );
        }
    };

    ConvexCollisionDetector::ConvexCollisionDetector(Rhi::DeviceContext &device_context) :
        m_impl(std::make_unique<Impl>(device_context)) {
    }

    ConvexCollisionDetector::~ConvexCollisionDetector() = default;

    void ConvexCollisionDetector::BindToScene(
        PhysicsScene &scene,
        const SpatialHashBroadDetector &broad_detector,
        uint32_t max_contact_points,
        float contact_margin
    ) {
        const bool config_changed = m_impl->bound_scene != &scene || m_impl->broad_detector != &broad_detector
                                    || m_impl->max_contact_points != std::max(1u, max_contact_points)
                                    || m_impl->contact_margin != contact_margin;

        m_impl->bound_scene = &scene;
        m_impl->broad_detector = &broad_detector;
        m_impl->max_contact_points = std::max(1u, max_contact_points);
        m_impl->contact_margin = contact_margin;

        // A changed configuration invalidates the preparation the next Record would
        // otherwise reuse; the pair capacity itself is observed there.
        if (config_changed) {
            m_impl->prepared = false;
        }
    }

    CollisionResultBuffers ConvexCollisionDetector::GetResultBuffers() const noexcept {
        CollisionResultBuffers result;
        result.collision_ids = m_impl->gpu_collision_ids.get();
        result.collision_normals = m_impl->gpu_collision_normals.get();
        result.contact_point_a = m_impl->gpu_contact_point_a.get();
        result.contact_point_b = m_impl->gpu_contact_point_b.get();
        result.collision_count = m_impl->gpu_collision_count.get();
        result.max_output_collision_pairs = m_impl->max_output_collision_pairs;
        return result;
    }

    void ConvexCollisionDetector::Record(vk::CommandBuffer cb) {
        assert(m_impl->bound_scene != nullptr && "BindToScene must be called before Record");

        // Prepare for the pair capacity the broad-phase detector currently reports.
        // Everything that allocates or acquires a kernel happens here, before the
        // call's first dispatch, and only when the observed capacity changed.
        {
            const BroadDetectorOutputBuffers broad = m_impl->broad_detector->GetResultBuffers();
            const uint32_t observed_shape_count = m_impl->bound_scene->GetGpuBuffers().shape_slot_count;
            // The broad detector's live pair buffers, not a reference held from a previous step.
            m_impl->pair_buffer = broad.pair_buffer;
            m_impl->pair_count_buffer = broad.pair_count_buffer;
            m_impl->max_input_collision_pairs = std::max(1u, broad.max_pairs);
            m_impl->shape_slot_count = observed_shape_count;

            const uint32_t output_pairs = std::max(1u, std::min(broad.max_pairs * 5u, m_impl->max_contact_points));
            if (!m_impl->prepared || m_impl->max_output_collision_pairs != output_pairs) {
                m_impl->max_output_collision_pairs = output_pairs;
                m_impl->prepared = true;
                m_impl->EnsureBuffers();
                m_impl->EnsureKernels();
            }
        }

        const auto gpu = m_impl->bound_scene->GetGpuBuffers();

        if (gpu.shape_alive == nullptr || gpu.shape_world_position == nullptr || gpu.shape_slot_count == 0u) {
            return;
        }
        if (m_impl->pair_buffer == nullptr || m_impl->pair_count_buffer == nullptr) {
            return;
        }

        DispatchBarrier(cb);

        // Every kernel was acquired during preparation above: recording creates no
        // pipeline.
        m_impl->clear_kernel->Dispatch(cb, {{"Target", *m_impl->gpu_collision_count}}, 1, 1, 1, 1u);

        DispatchBarrier(cb);

        const DetectPushParams params{
            m_impl->contact_margin, m_impl->shape_slot_count, m_impl->max_output_collision_pairs
        };

        const uint32_t detect_wg = std::max(1u, (m_impl->max_input_collision_pairs + 63u) / 64u);
        m_impl->detect_kernel->Dispatch(
            cb,
            {{"ShapeAlive", *gpu.shape_alive},
             {"ShapeType", *gpu.shape_type},
             {"ShapeFeature", *gpu.shape_feature},
             {"ShapeWorldPosition", *gpu.shape_world_position},
             {"ShapeWorldRotation", *gpu.shape_world_rotation},
             {"CollisionPairs", *m_impl->pair_buffer},
             {"PairCount", *m_impl->pair_count_buffer},
             {"CollisionIds", *m_impl->gpu_collision_ids},
             {"CollisionNormals", *m_impl->gpu_collision_normals},
             {"ContactPointA", *m_impl->gpu_contact_point_a},
             {"ContactPointB", *m_impl->gpu_contact_point_b},
             {"CollisionCount", *m_impl->gpu_collision_count}},
            detect_wg,
            1,
            1,
            params
        );
    }
} // namespace Engine
