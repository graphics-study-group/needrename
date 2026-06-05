#ifndef ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED
#define ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED

#include <memory>

namespace Engine {
    class PhysicsScene;
    class RenderGraphBuilder;
    class RenderSystem;

    /**
     * @brief XPBD GPU solver with lazy shader compilation.
     *
     * The solver owns a ComputeStage and ComputeResourceBinding created once
     * on the first call to Step(). Step() populates a RenderGraphBuilder2 with
     * compute passes that import GPU buffers from PhysicsScene. The caller
     * builds the render graph from the builder and executes it each frame.
     */
    class XPBDGpuSolver {
    public:
        /**
         * @brief Construct the solver.
         *
         * No GPU resources are allocated until the first call to Step().
         *
         * @param render_system Render system used for pipeline creation.
         */
        explicit XPBDGpuSolver(RenderSystem &render_system);

        /**
         * @brief Destroy the solver and release all GPU resources.
         */
        ~XPBDGpuSolver();

        XPBDGpuSolver(const XPBDGpuSolver &) = delete;
        XPBDGpuSolver &operator=(const XPBDGpuSolver &) = delete;
        XPBDGpuSolver(XPBDGpuSolver &&) = delete;
        XPBDGpuSolver &operator=(XPBDGpuSolver &&) = delete;

        /**
         * @brief Fill a render graph builder with XPBD compute passes.
         *
         * On first call, lazily compiles the compute shader and creates
         * the ComputeStage. GPU buffers from the physics scene are imported as
         * external resources so the render graph handles barriers automatically.
         *
         * @param builder  Render graph builder to populate with passes.
         * @param physics_scene  Physics scene providing GPU buffers.
         */
        void Step(RenderGraphBuilder &builder, PhysicsScene &physics_scene);

        /**
         * @brief Return whether the solver has been lazily initialized.
         */
        bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED
