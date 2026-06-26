#ifndef ENGINE_PHYSICS_SOLVER_ISOLVER_INCLUDED
#define ENGINE_PHYSICS_SOLVER_ISOLVER_INCLUDED

namespace vk {
    struct CommandBuffer;
}

namespace Engine {
    class RenderSystem;
    class PhysicsScene;

    /**
     * @brief Abstract base class for GPU physics solvers.
     *
     * Solvers own their RenderGraph instances internally and are driven
     * by PhysicsSystem via the three-phase PreGPUStep → GPUStep →
     * PostGPUStep lifecycle.
     *
     * PreGPUStep / PostGPUStep run outside the CommandBuffer scope.
     * GPUStep receives the CommandBuffer and records its RenderGraph
     * passes directly — callers never access the solver's RG.
     */
    class ISolver {
    public:
        virtual ~ISolver() = default;

        /**
         * @brief Called BEFORE cb.begin() each frame.
         *
         * Hook for CPU-side preparation work such as uploading new
         * data to GPU buffers. Default implementation is a no-op.
         */
        virtual void PreGPUStep(RenderSystem & /*system*/, PhysicsScene & /*scene*/) {}

        /**
         * @brief Called BETWEEN cb.begin() and cb.end() each frame.
         *
         * The solver may lazily create its RenderGraph on the first
         * call. It MUST record its passes to @p cb before returning.
         *
         * @param cb CommandBuffer in Recording state (after begin, before end).
         */
        virtual void GPUStep(RenderSystem &system, PhysicsScene &scene, vk::CommandBuffer cb) = 0;

        /**
         * @brief Called AFTER cb.end() + submit each frame.
         *
         * Hook for GPU→CPU readback or post-processing work.
         * Default implementation is a no-op.
         */
        virtual void PostGPUStep(RenderSystem & /*system*/, PhysicsScene & /*scene*/) {}

        /**
         * @brief Check whether the solver has been fully initialized.
         */
        [[nodiscard]]
        virtual bool IsInitialized() const noexcept = 0;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_SOLVER_ISOLVER_INCLUDED
