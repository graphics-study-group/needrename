#ifndef ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED
#define ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED

#include <memory>

namespace vk {
    class CommandBuffer;
}
namespace Engine {
    namespace Rhi {
        class ComputeBuffer;
    }
    class PhysicsScene;
    class SpatialHashBroadDetector;
    namespace Rhi {
        class DeviceContext;
    }

    /**
     * @brief Bundle of read-only pointers to collision detection result buffers.
     *
     * Obtained from ConvexCollisionDetector::GetResultBuffers().  All buffers are
     * owned by the detector and live until the detector is destroyed.
     */
    struct CollisionResultBuffers {
        const Rhi::ComputeBuffer *collision_ids{};
        const Rhi::ComputeBuffer *collision_normals{};
        const Rhi::ComputeBuffer *contact_point_a{};
        const Rhi::ComputeBuffer *contact_point_b{};
        const Rhi::ComputeBuffer *collision_count{};
        uint32_t max_output_collision_pairs{0};
    };

    /**
     * @brief GPU narrow-phase convex collision detection using MPR algorithm.
     *
     * ConvexCollisionDetector owns the MPR collision detection compute pipeline
     * (detect_collisions.comp).  Collision pairs to test come from the broad-phase
     * detector, whose live output is read at preparation time.
     *
     * Lifecycle:
     *   1. Construct with Rhi::DeviceContext& only (no GPU allocation).
     *   2. BindToScene(scene, broad_detector, max_contact_points, contact_margin)
     *      -- caches CPU values and the broad-phase source only, no allocation.
     *   3. Record(cb) -- prepares itself for the pair capacity it observes and
     *      dispatches compute passes directly to cb. Preparation is a no-op when
     *      nothing changed.
     *
     * Collision results are stored in separate SoA GPU buffers:
     *   - collision_ids:       uvec2 (shape_a, shape_b)
     *   - collision_normals:   vec4  (xyz = normal, w = penetration depth)
     *   - contact_point_a:     vec4  (contact point on A, shape-local space)
     *   - contact_point_b:     vec4  (contact point on B, shape-local space)
     *   - collision_count:     uint  (total contact points, each atomicAdd'd)
     *
     * Each collision pair may produce up to 5 contact entries (4 perturbation
     * + optionally 1 MPR fallback).  All result buffers are sized to
     * max_collision_pairs * 5.
     */
    class ConvexCollisionDetector {
    public:
        explicit ConvexCollisionDetector(Rhi::DeviceContext &device_context);
        ~ConvexCollisionDetector();

        ConvexCollisionDetector(const ConvexCollisionDetector &) = delete;
        ConvexCollisionDetector &operator=(const ConvexCollisionDetector &) = delete;
        ConvexCollisionDetector(ConvexCollisionDetector &&) = delete;
        ConvexCollisionDetector &operator=(ConvexCollisionDetector &&) = delete;

        /**
         * @brief Bind the detector to its scene and to the broad-phase source it reads.
         *
         * @param scene               Scene whose shape buffers are read.
         * @param broad_detector      Broad-phase detector whose pair buffers are read.
         * @param max_contact_points  Upper bound on contact points written per step.
         * @param contact_margin      Contact margin for penetration validation.
         */
        void BindToScene(
            PhysicsScene &scene,
            const SpatialHashBroadDetector &broad_detector,
            uint32_t max_contact_points,
            float contact_margin
        );

        /**
         * @brief GPU-side: prepare for the observed pair capacity, then record dispatches.
         *
         * Takes the broad detector's current pair buffers, sizes its result buffers
         * and acquires its kernels before the first dispatch, and only when the
         * observed pair capacity or shape count changed. Inserts a MemoryBarrier2
         * at the start.
         */
        void Record(vk::CommandBuffer cb);

        /**
         * @brief Get read-only pointers to result buffers.
         *
         * Valid after the first Record() (which sizes them).
         * Pointers are stable for the detector's lifetime.
         */
        CollisionResultBuffers GetResultBuffers() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED
