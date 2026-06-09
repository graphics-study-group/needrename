#ifndef ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED
#define ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED

#include <memory>

namespace Engine {
    class PhysicsScene;
    class RenderGraphBuilder;
    class RenderSystem;

    // Forward declaration from Render/Pipeline/RenderGraph/RGAttachmentDesc.h
    enum class RGBufferHandle : int32_t;

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
        /**
         * @brief Fill a render graph builder with XPBD compute passes.
         *
         * @param builder  Render graph builder to populate with passes.
         * @param physics_scene  Physics scene providing GPU buffers.
         * @param external_model_matrices_handle  Optional pre-imported handle
         *        for the model matrices buffer.  When provided (non-empty, i.e.
         *        not `RGBufferHandle{}`), the solver skips its own import and
         *        uses this handle instead.  This allows the caller to share the
         *        handle with subsequent passes (e.g. a rendering pass that reads
         *        the model matrices) so the render graph can insert correct
         *        barriers.
         */
        void Step(
            RenderGraphBuilder &builder,
            PhysicsScene &physics_scene,
            RGBufferHandle external_model_matrices_handle = RGBufferHandle{}
        );

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
