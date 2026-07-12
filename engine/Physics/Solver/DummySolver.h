#ifndef ENGINE_PHYSICS_SOLVER_DUMMYSOLVER_INCLUDED
#define ENGINE_PHYSICS_SOLVER_DUMMYSOLVER_INCLUDED

#include "ISolver.h"

#include <memory>

namespace Engine {
    class CommandBuffer;
    class ComputeBuffer;
    class ComputeStage;
    class RenderSystem;
    class PhysicsScene;
    struct XpbdConfig;

    /**
     * @brief Minimal GPU solver that displaces bodies by gravity and writes
     * model matrices.
     *
     * DummySolver displaces all rigid bodies by
     *   delta_z = gravity.z * time_step
     * each frame and writes model matrices from the updated pose.
     *
     * Compute dispatch is recorded directly to the command buffer in GPUStep
     * (no RenderGraph).
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

        void PreGPUStep() override;
        void GPUStep(CommandBuffer &command_buffer) override;

        [[nodiscard]]
        bool IsInitialized() const noexcept override;

        void SetConfig(const XpbdConfig &config) noexcept;
        const XpbdConfig &GetConfig() const noexcept;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_SOLVER_DUMMYSOLVER_INCLUDED
