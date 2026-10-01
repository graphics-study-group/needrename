#ifndef ENGINE_PHYSICS_SPATIALHASHBROADDETECTOR_INCLUDED
#define ENGINE_PHYSICS_SPATIALHASHBROADDETECTOR_INCLUDED

#include <glm.hpp>
#include <memory>

namespace vk {
    class CommandBuffer;
}
namespace Engine {
    namespace Rhi {
        class ComputeBuffer;
    }
    class PhysicsScene;
    namespace Rhi {
        class DeviceContext;
    }

    /**
     * @brief Spatial hash grid configuration.
     */
    struct GridConfig {
        glm::vec3 world_min{-100.0f, -100.0f, -100.0f};
        glm::vec3 world_max{100.0f, 100.0f, 100.0f};
        float cell_size = 2.0f;
        uint32_t max_cells_per_shape = 8;
    };

    /**
     * @brief Bundled raw output buffers from the broad-phase detector.
     *
     * Obtained via GetResultBuffers().  References are guaranteed valid for the
     * detector's lifetime.
     */
    struct BroadDetectorOutputBuffers {
        const Rhi::ComputeBuffer *pair_buffer{};
        const Rhi::ComputeBuffer *pair_count_buffer{};
        uint32_t max_pairs{};
    };

    /**
     * @brief GPU spatial-hash broad-phase collision detector.
     *
     * SpatialHashBroadDetector owns GPU compute pipelines and buffers for
     * spatial-hash-based candidate pair generation.
     *
     * When shape_count <= fallback_all_pairs_threshold, the entire spatial-hash
     * pipeline is skipped in favour of direct all-pairs generation.
     *
     * Lifecycle:
     *   1. Construct with Rhi::DeviceContext& only (no GPU allocation).
     *   2. BindToScene(scene, grid_config, threshold, max_global_shape_count) --
     *      caches CPU values only, no allocation.
     *   3. Record(cb) -- prepares itself for the geometry it observes (sizing,
     *      kernel acquisition, per-dispatch constants) and dispatches compute
     *      passes directly to cb. Preparation is a no-op when nothing changed.
     */
    class SpatialHashBroadDetector {
    public:
        explicit SpatialHashBroadDetector(Rhi::DeviceContext &device_context);
        ~SpatialHashBroadDetector();

        SpatialHashBroadDetector(const SpatialHashBroadDetector &) = delete;
        SpatialHashBroadDetector &operator=(const SpatialHashBroadDetector &) = delete;
        SpatialHashBroadDetector(SpatialHashBroadDetector &&) = delete;
        SpatialHashBroadDetector &operator=(SpatialHashBroadDetector &&) = delete;

        /**
         * @brief Bind the detector to the scene it observes and to its configuration.
         *
         * @param scene                       Scene whose shape buffers are read.
         * @param grid_config                 Spatial hash grid bounds and cell size.
         * @param fallback_all_pairs_threshold Shape count at or below which the
         *                                    all-pairs fallback runs.
         * @param max_global_shape_count      Global-shape dispatch bound.
         */
        void BindToScene(
            PhysicsScene &scene,
            const GridConfig &grid_config,
            uint32_t fallback_all_pairs_threshold,
            uint32_t max_global_shape_count
        );

        /**
         * @brief GPU-side: prepare for the observed geometry, then record dispatches.
         *
         * Sizes its buffers, acquires its kernels and prepares its per-dispatch
         * constants before the first dispatch, and only when the observed shape
         * count changed since the previous call. Inserts a MemoryBarrier2 at the
         * start. The path (fallback vs spatial-hash) is selected by the threshold
         * cached by BindToScene().
         */
        void Record(vk::CommandBuffer cb);

        BroadDetectorOutputBuffers GetResultBuffers() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_SPATIALHASHBROADDETECTOR_INCLUDED
