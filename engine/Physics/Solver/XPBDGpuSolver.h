#ifndef ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED
#define ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED

#include <glm.hpp>
#include <memory>

namespace Engine {
    class ComputeBuffer;
    class PhysicsScene;
    class RenderGraphBuilder;
    class RenderSystem;

    // Forward declaration from Render/Pipeline/RenderGraph/RGAttachmentDesc.h
    enum class RGBufferHandle : int32_t;

    /**
     * @brief XPBD configuration parameters.
     */
    struct XpbdConfig {
        glm::vec3 gravity{0.0f, 0.0f, -9.81f};
        float     time_step = 1.0f / 100.0f;
        uint32_t  num_substep_perstep = 1;
        uint32_t  num_iter_persubstep = 5;
        uint32_t  num_velocity_iters  = 5;
        uint32_t  max_contact_points  = 100000u;
    };

    /**
     * @brief XPBD GPU solver with multi-pass compute shader dispatch.
     *
     * Implements a full GPU XPBD contact solver:
     *   - Semi-implicit Euler force integration (gravity + external)
     *   - Shape world pose update (body pose → shape world transforms)
     *   - Per-substep collision detection (owned internally)
     *   - Jacobi contact position solve with lagrange accumulation
     *   - Velocity update from pose delta
     *   - Velocity-level friction + restitution solve
     *
     * All intermediate GPU buffers (snapshots, accumulators, lagrange) are
     * owned by the solver and sized lazily on first Step().
     * Collision detection is managed internally — external callers do not
     * need to create or pass a ConvexCollisionDetector.
     */
    class XPBDGpuSolver {
    public:
        explicit XPBDGpuSolver(RenderSystem &render_system);
        ~XPBDGpuSolver();

        XPBDGpuSolver(const XPBDGpuSolver &) = delete;
        XPBDGpuSolver &operator=(const XPBDGpuSolver &) = delete;
        XPBDGpuSolver(XPBDGpuSolver &&) = delete;
        XPBDGpuSolver &operator=(XPBDGpuSolver &&) = delete;

        /**
         * @brief Fill a render graph builder with XPBD compute passes.
         *
         * @param builder       Render graph builder to populate.
         * @param physics_scene Physics scene providing GPU body and shape buffers.
         * @param external_model_matrices_handle Optional pre-imported model
         *        matrices buffer handle for sharing with rendering passes.
         */
        void Step(
            RenderGraphBuilder &builder,
            PhysicsScene &physics_scene,
            RGBufferHandle external_model_matrices_handle = RGBufferHandle{}
        );

        bool IsInitialized() const noexcept;
        void SetConfig(const XpbdConfig &config) noexcept;
        const XpbdConfig &GetConfig() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED
