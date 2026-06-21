#ifndef ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED
#define ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED

#include <memory>

namespace Engine {
    class ComputeBuffer;
    class PhysicsScene;
    class RenderGraphBuilder;
    class RenderSystem;

    /**
     * @brief Bundle of read-only pointers to collision detection result buffers.
     *
     * Returned by ConvexCollisionDetector::GetCollisionResultBuffers().  All
     * buffers are owned by the detector and live until the detector is destroyed.
     */
    struct CollisionResultBuffers {
        const ComputeBuffer *collision_ids{};
        const ComputeBuffer *collision_normals{};
        const ComputeBuffer *contact_point_a{};
        const ComputeBuffer *contact_point_b{};
        const ComputeBuffer *collision_count{};
        uint32_t max_collision_pairs{0};
    };

    /**
     * @brief GPU convex collision detection using MPR algorithm.
     *
     * ConvexCollisionDetector owns two compute shader pipelines:
     *   1. generate_pairs.comp  — GPU-side all-pairs pair generation
     *   2. detect_collisions.comp — MPR collision detection + result output
     *
     * Collision results are stored in separate SoA GPU buffers:
     *   - collision_ids:       uvec2 (shape_a, shape_b)
     *   - collision_normals:   vec4  (xyz = normal, w = penetration depth)
     *   - contact_point_a:     vec4  (contact point on A, world space)
     *   - contact_point_b:     vec4  (contact point on B, world space)
     *   - collision_count:     uint  (total contact points, each atomicAdd'd)
     *
     * Each collision pair may produce up to 5 contact entries (4 perturbation
     * + optionally 1 MPR fallback).  All buffers are sized to max_collision_pairs
     * at construction time.
     * The detector follows the same lazy-SPIR-V-loading pattern as XPBDGpuSolver.
     */
    class ConvexCollisionDetector {
    public:
        /**
         * @brief Construct the detector.
         *
         * No GPU resources are allocated until the first call to Step().
         *
         * @param render_system       Render system used for pipeline creation.
         * @param max_collision_pairs Maximum number of collision contact entries
         *                            (pairs × up to 5 points each).
         * @param contact_margin      Contact margin for penetration validation.
         *                            Points with separation < margin are retained
         *                            as speculative contacts.
         */
        explicit ConvexCollisionDetector(
            RenderSystem &render_system, uint32_t max_collision_pairs, float contact_margin = 0.001f
        );

        /**
         * @brief Destroy the detector and release all GPU resources.
         */
        ~ConvexCollisionDetector();

        ConvexCollisionDetector(const ConvexCollisionDetector &) = delete;
        ConvexCollisionDetector &operator=(const ConvexCollisionDetector &) = delete;
        ConvexCollisionDetector(ConvexCollisionDetector &&) = delete;
        ConvexCollisionDetector &operator=(ConvexCollisionDetector &&) = delete;

        /**
         * @brief Fill a render graph builder with collision detection passes.
         *
         * On first call, lazily compiles the compute shaders and creates the
         * ComputeStage instances.  GPU buffers from the physics scene are
         * imported as external resources.
         *
         * Adds two compute passes:
         *   1. GPU pair generation (generate_pairs.comp)
         *   2. Collision detection (detect_collisions.comp)
         *
         * The collision_count buffer is reset to 0 before dispatch.
         *
         * @param builder        Render graph builder to populate with passes.
         * @param physics_scene  Physics scene providing GPU shape buffers.
         */
        void Step(RenderGraphBuilder &builder, PhysicsScene &physics_scene);

        /**
         * @brief Return whether the detector has been lazily initialized.
         */
        bool IsInitialized() const noexcept;

        /**
         * @brief Get the collision result buffers.
         *
         * Returns a struct of read-only pointers to the detector's owned result
         * buffers.  The caller reads these after the render graph executes.
         */
        CollisionResultBuffers GetCollisionResultBuffers() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED
