#ifndef ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED
#define ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED

#include <memory>

namespace Engine {
    class ComputeBuffer;
    class PhysicsScene;
    class RenderGraphBuilder;
    class RenderSystem;
    enum class RGBufferHandle : int32_t;
    struct PhysicsSceneBufferHandles;

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
     * @brief Pre-imported RenderGraph handles for narrow-phase result buffers.
     *
     * Returned by AddDetectPasses() so the solver can consume the handles
     * directly without re-importing detector-owned buffers.
     */
    struct NarrowDetectorOutputHandles {
        RGBufferHandle collision_ids{};
        RGBufferHandle collision_normals{};
        RGBufferHandle contact_point_a{};
        RGBufferHandle contact_point_b{};
        RGBufferHandle collision_count{};
    };

    /**
     * @brief GPU narrow-phase convex collision detection using MPR algorithm.
     *
     * ConvexCollisionDetector owns the MPR collision detection compute pipeline
     * (detect_collisions.comp).  Collision pairs to test are provided externally
     * by the broad-phase detector via GPU buffers.
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
     */
    class ConvexCollisionDetector {
    public:
        explicit ConvexCollisionDetector(
            RenderSystem &render_system, uint32_t max_collision_pairs, float contact_margin = 0.001f
        );

        ~ConvexCollisionDetector();

        ConvexCollisionDetector(const ConvexCollisionDetector &) = delete;
        ConvexCollisionDetector &operator=(const ConvexCollisionDetector &) = delete;
        ConvexCollisionDetector(ConvexCollisionDetector &&) = delete;
        ConvexCollisionDetector &operator=(ConvexCollisionDetector &&) = delete;

        /**
         * @brief Fill a render graph builder with the narrow-phase collision pass.
         *
         * Collision pairs are read from the external @p pair_buffer (produced
         * by the broad-phase detector).  @p pair_count_buffer provides the actual
         * number of pairs to test; threads beyond that count early-return.
         *
         * @param builder             Render graph builder to populate.
         * @param physics_scene       Physics scene providing GPU shape buffers.
         * @param pair_buffer         External GPU uvec2[] pair buffer (broad-phase output).
         * @param pair_count_buffer   External GPU uint pair count buffer.
         * @param handles             Pre-imported RenderGraph handles for scene-owned
         *                            shape buffers.
         * @param pair_buffer_handle  Pre-imported handle for pair_buffer.
         * @param pair_count_handle   Pre-imported handle for pair_count_buffer.
         * @return Handles to detector-owned collision result buffers.
         */
        NarrowDetectorOutputHandles AddDetectPasses(
            RenderGraphBuilder &builder,
            PhysicsScene &physics_scene,
            const ComputeBuffer &pair_buffer,
            const ComputeBuffer &pair_count_buffer,
            const PhysicsSceneBufferHandles &handles,
            RGBufferHandle pair_buffer_handle,
            RGBufferHandle pair_count_handle
        );

        bool IsInitialized() const noexcept;

        CollisionResultBuffers GetCollisionResultBuffers() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_CONVEXCOLLISIONDETECTOR_INCLUDED
