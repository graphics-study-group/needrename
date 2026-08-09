#ifndef ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED
#define ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED

#include "ISolver.h"

#include <glm.hpp>
#include <memory>

namespace Engine {
    namespace Rhi {
        class ComputeBuffer;
        class ComputeStage;
    } // namespace Rhi
    class ConvexCollisionDetector;
    class PhysicsScene;
    namespace Rhi {
        class DeviceContext;
    }
    class SpatialHashBroadDetector;

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

        glm::vec3 grid_world_min{-100.0f, -100.0f, -100.0f};
        glm::vec3 grid_world_max{100.0f, 100.0f, 100.0f};
        float grid_cell_size = 2.0f;
        uint32_t max_cells_per_shape = 8;
        uint32_t max_global_shape_count = 128;
        uint32_t fallback_all_pairs_threshold = 32;
    };

    /**
     * @brief XPBD GPU solver with direct compute dispatch.
     *
     * Inherits ISolver.  Owns compute pipelines, resource bindings, and
     * intermediate buffers.  All GPU dispatches are recorded directly to the
     * command buffer in GPUStep.
     *
     * Lifecycle:
     *   1. Construct with Rhi::DeviceContext&.
     *   2. OnBindToScene(scene) -- called by PhysicsSystem during registration.
     *   3. PreGPUStep() -- shader loading, buffer sizing, CPU uploads, detector Configure.
     *   4. GPUStep(cb) -- record compute dispatches with manual barriers.
     */
    class XpbdGpuSolver : public ISolver {
    public:
        explicit XpbdGpuSolver(Rhi::DeviceContext &device_context);
        ~XpbdGpuSolver() override;

        XpbdGpuSolver(const XpbdGpuSolver &) = delete;
        XpbdGpuSolver &operator=(const XpbdGpuSolver &) = delete;
        XpbdGpuSolver(XpbdGpuSolver &&) = delete;
        XpbdGpuSolver &operator=(XpbdGpuSolver &&) = delete;

        void PreGPUStep() override;
        void GPUStep(vk::CommandBuffer cb) override;
        bool IsInitialized() const noexcept override;

        void SetConfig(const XpbdConfig &config) noexcept;
        const XpbdConfig &GetConfig() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_XPBDGPUSOLVER_INCLUDED
