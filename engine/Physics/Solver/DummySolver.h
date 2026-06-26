#ifndef ENGINE_PHYSICS_SOLVER_DUMMYSOLVER_INCLUDED
#define ENGINE_PHYSICS_SOLVER_DUMMYSOLVER_INCLUDED

#include "ISolver.h"

#include <memory>

namespace Engine {
    class ComputeBuffer;
    class ComputeStage;
    class RenderSystem;
    class PhysicsScene;
    class RenderGraph;
    class RenderGraphBuilder;
    enum class RGBufferHandle : int32_t;
    struct XpbdConfig;

    /**
     * @brief Minimal GPU solver for validating the separate physics
     * RenderGraph architecture.
     *
     * DummySolver displaces all rigid bodies along -Z by
     *   delta_z = gravity.z * time_step
     * each frame and writes model matrices from the updated pose.
     *
     * It owns a single RenderGraph created lazily on the first
     * GPUStep() call and reused thereafter. The RG is private —
     * GPUStep() records it to the provided CommandBuffer directly.
     */
    class DummySolver : public ISolver {
        struct Impl;
        std::unique_ptr<Impl> m_impl;

    public:
        explicit DummySolver(RenderSystem &render_system);
        ~DummySolver() override;

        DummySolver(const DummySolver &) = delete;
        DummySolver &operator=(const DummySolver &) = delete;
        DummySolver(DummySolver &&) = delete;
        DummySolver &operator=(DummySolver &&) = delete;

        // ISolver interface
        void PreGPUStep() override;
        void GPUStep(vk::CommandBuffer cb) override;

        [[nodiscard]]
        bool IsInitialized() const noexcept override;

        // PostGPUStep uses default no-op implementation.

        /**
         * @brief Set XPBD configuration parameters.
         *
         * Only gravity and time_step are used by DummySolver;
         * other fields are ignored.
         */
        void SetConfig(const XpbdConfig &config) noexcept;

        /**
         * @brief Get current configuration.
         */
        const XpbdConfig &GetConfig() const noexcept;

    private:
        std::unique_ptr<RenderGraph> BuildRenderGraph();
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_SOLVER_DUMMYSOLVER_INCLUDED
