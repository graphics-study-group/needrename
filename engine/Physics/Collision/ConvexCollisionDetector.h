#ifndef ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED
#define ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED

#include <memory>

namespace Engine {
    class CommandBuffer;
    class ComputeBuffer;
    class PhysicsScene;
    class RenderSystem;

    /**
     * @brief Bundle of read-only pointers to collision detection result buffers.
     *
     * Obtained from ConvexCollisionDetector::GetResultBuffers().  All buffers are
     * owned by the detector and live until the detector is destroyed.
     */
    struct CollisionResultBuffers {
        const ComputeBuffer *collision_ids{};
        const ComputeBuffer *collision_normals{};
        const ComputeBuffer *contact_point_a{};
        const ComputeBuffer *contact_point_b{};
        const ComputeBuffer *collision_count{};
        uint32_t max_output_collision_pairs{0};
    };

    /**
     * @brief GPU narrow-phase convex collision detection using MPR algorithm.
     *
     * ConvexCollisionDetector owns the MPR collision detection compute pipeline
     * (detect_collisions.comp).  Collision pairs to test are provided by the
     * broad-phase detector -- pair buffer references are cached during Configure().
     *
     * Lifecycle:
     *   1. Construct with RenderSystem& only (no GPU allocation).
     *   2. Configure(scene, max_pairs, margin, pair_buf, count_buf) -- CPU prep,
     *      buffer allocation, shader loading, binding creation.
     *   3. Record(cb) -- dispatch compute passes directly to cb, return void.
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
        explicit ConvexCollisionDetector(RenderSystem &render_system);
        ~ConvexCollisionDetector();

        ConvexCollisionDetector(const ConvexCollisionDetector &) = delete;
        ConvexCollisionDetector &operator=(const ConvexCollisionDetector &) = delete;
        ConvexCollisionDetector(ConvexCollisionDetector &&) = delete;
        ConvexCollisionDetector &operator=(ConvexCollisionDetector &&) = delete;

        /**
         * @brief CPU-side preparation: cache references, size buffers, upload config,
         *        load shaders, allocate resource bindings.
         *
         * Safe to call every frame -- no-op when nothing changed.
         *
         * @param scene                    Physics scene for GPU buffer access.
         * @param max_input_collision_pairs  Maximum number of candidate pairs to test.
         * @param max_output_collision_pairs Maximum number of output collision pairs.
         * @param contact_margin           Contact margin for penetration validation.
         * @param pair_buffer              Broad-phase output: uvec2 pair buffer.
         * @param pair_count_buffer        Broad-phase output: uint pair count buffer.
         */
        void Configure(
            PhysicsScene &scene,
            uint32_t max_input_collision_pairs,
            uint32_t max_output_collision_pairs,
            float contact_margin,
            const ComputeBuffer &pair_buffer,
            const ComputeBuffer &pair_count_buffer
        );

        /**
         * @brief GPU-side: record compute dispatches directly to the command buffer.
         *
         * Must be called after Configure().  Inserts a MemoryBarrier2 at the start.
         * All passes dispatch via BindComputeStage / DispatchCompute directly
         * (no RenderGraph).
         */
        void Record(CommandBuffer &cb);

        bool IsInitialized() const noexcept;

        /**
         * @brief Get read-only pointers to result buffers.
         *
         * Valid after first Configure() (which calls EnsureBuffers).
         * Pointers are stable for the detector's lifetime.
         */
        CollisionResultBuffers GetResultBuffers() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED
