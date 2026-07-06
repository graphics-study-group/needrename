#ifndef ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED
#define ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED

#include "ISolver.h"

#include <glm.hpp>
#include <memory>

namespace Engine {
    class ComputeBuffer;
    class ComputeStage;
    class ConvexCollisionDetector;
    class PhysicsScene;
    class RenderGraph;
    class RenderSystem;
    class SpatialHashBroadDetector;
    enum class RGBufferHandle : int32_t;

    /**
     * @brief XPBD configuration parameters.
     */
    struct XpbdConfig {
        glm::vec3 gravity{0.0f, 0.0f, -9.81f};
        float time_step = 1.0f / 60.0f;
        uint32_t num_substep_perstep = 2;
        uint32_t num_iter_persubstep = 20;
        uint32_t num_velocity_iters = 20;
        uint32_t max_contact_points = 100000u;
        float contact_margin = 0.001f;

        // Broad-phase spatial hash grid configuration.
        glm::vec3 grid_world_min{-100.0f, -100.0f, -100.0f};
        glm::vec3 grid_world_max{100.0f, 100.0f, 100.0f};
        float grid_cell_size = 2.0f;
        uint32_t max_cells_per_shape = 8;
        uint32_t max_global_shape_count = 128;
        uint32_t fallback_all_pairs_threshold = 32;
    };

    /**
     * @brief XPBD GPU solver with multi-RenderGraph architecture.
     *
     * Inherits ISolver.  Owns multiple RenderGraphs, each representing a
     * distinct physics phase.  RGs are built lazily and recorded in sequence
     * during GPUStep via CPU-side substep / iteration loops.
     *
     * Lifecycle:
     *   1. Construct with RenderSystem&.
     *   2. OnBindToScene(scene) — called by PhysicsSystem during registration.
     *   3. PreGPUStep() — shader loading, buffer sizing, CPU uploads, detector Configure.
     *   4. GPUStep(cb) — lazy-build RGs, record in sequence with loops.
     */
    class XpbdGpuSolver : public ISolver {
    public:
        explicit XpbdGpuSolver(RenderSystem &render_system);
        ~XpbdGpuSolver() override;

        XpbdGpuSolver(const XpbdGpuSolver &) = delete;
        XpbdGpuSolver &operator=(const XpbdGpuSolver &) = delete;
        XpbdGpuSolver(XpbdGpuSolver &&) = delete;
        XpbdGpuSolver &operator=(XpbdGpuSolver &&) = delete;

        // ISolver interface
        void PreGPUStep() override;
        void GPUStep(vk::CommandBuffer cb) override;
        bool IsInitialized() const noexcept override;

        void SetConfig(const XpbdConfig &config) noexcept;
        const XpbdConfig &GetConfig() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;

        // RG build helpers.
        std::unique_ptr<RenderGraph> BuildPreCollisionRG();
        std::unique_ptr<RenderGraph> BuildPostCollisionPreIterRG();
        std::unique_ptr<RenderGraph> BuildPositionIterRG();
        std::unique_ptr<RenderGraph> BuildPostPositionRG();
        std::unique_ptr<RenderGraph> BuildVelocityIterRG();
        std::unique_ptr<RenderGraph> BuildModelMatrixRG();
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED
