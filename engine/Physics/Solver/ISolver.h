#ifndef ENGINE_PHYSICS_SOLVER_ISOLVER_INCLUDED
#define ENGINE_PHYSICS_SOLVER_ISOLVER_INCLUDED

namespace vk {
    struct CommandBuffer;
}

namespace Engine {
    class PhysicsScene;

    /**
     * @brief Abstract base class for GPU physics solvers.
     *
     * Solvers own their compute pipelines and resource bindings internally
     * and are driven by PhysicsSystem via the three-phase PreGPUStep →
     * GPUStep → PostGPUStep lifecycle.
     *
     * PreGPUStep / PostGPUStep run outside the CommandBuffer scope.
     * GPUStep receives the CommandBuffer and records compute dispatches
     * directly — callers never access the solver's internal resources.
     *
     * Each solver is bound to a specific PhysicsScene at registration
     * time via OnBindToScene(). The bound scene is accessible through
     * the protected m_bound_scene member. RenderSystem is stored by
     * each concrete solver at construction time.
     */
    class ISolver {
    public:
        virtual ~ISolver() = default;

        /**
         * @brief Bind this solver to a specific PhysicsScene.
         *
         * Called by PhysicsSystem::RegisterSolver() after verifying the
         * scene exists. The default implementation stores the pointer.
         * Subclasses may override for additional setup, but must call
         * ISolver::OnBindToScene() to ensure m_bound_scene is set.
         *
         * @param scene The PhysicsScene this solver operates on.
         */
        virtual void OnBindToScene(PhysicsScene &scene) {
            m_bound_scene = &scene;
        }

        /**
         * @brief Called BEFORE cb.begin() each frame.
         *
         * Hook for CPU-side preparation work such as uploading new
         * data to GPU buffers. The solver accesses its scene through
         * m_bound_scene. Default implementation is a no-op.
         */
        virtual void PreGPUStep() {
        }

        /**
         * @brief Called BETWEEN cb.begin() and cb.end() each frame.
         *
         * The solver MUST record its compute dispatches to @p cb before
         * returning. The solver accesses its scene through m_bound_scene.
         *
         * @param cb CommandBuffer in Recording state (after begin, before end).
         */
        virtual void GPUStep(vk::CommandBuffer cb) = 0;

        /**
         * @brief Called AFTER cb.end() + submit each frame.
         *
         * Hook for GPU→CPU readback or post-processing work.
         * The solver accesses its scene through m_bound_scene.
         * Default implementation is a no-op.
         */
        virtual void PostGPUStep() {
        }

        /**
         * @brief Check whether the solver has been fully initialized.
         */
        [[nodiscard]]
        virtual bool IsInitialized() const noexcept = 0;

    protected:
        /// The PhysicsScene this solver is bound to. Set by OnBindToScene().
        PhysicsScene *m_bound_scene = nullptr;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_SOLVER_ISOLVER_INCLUDED
