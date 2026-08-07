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
     *   2. Configure(scene, shape_count, grid_config, threshold) -- CPU prep,
     *      buffer allocation, shader loading, binding creation.
     *   3. Record(cb) -- dispatch compute passes directly to cb, return void.
     */
    class SpatialHashBroadDetector {
    public:
        explicit SpatialHashBroadDetector(Rhi::DeviceContext &device_context);
        ~SpatialHashBroadDetector();

        SpatialHashBroadDetector(const SpatialHashBroadDetector &) = delete;
        SpatialHashBroadDetector &operator=(const SpatialHashBroadDetector &) = delete;
        SpatialHashBroadDetector(SpatialHashBroadDetector &&) = delete;
        SpatialHashBroadDetector &operator=(SpatialHashBroadDetector &&) = delete;

        void Configure(
            PhysicsScene &scene,
            uint32_t shape_count,
            const GridConfig &grid_config,
            uint32_t fallback_all_pairs_threshold,
            uint32_t max_global_shape_count
        );

        /**
         * @brief GPU-side: record compute dispatches directly to the command buffer.
         *
         * Must be called after Configure().  Inserts a MemoryBarrier2 at the start.
         * The path (fallback vs spatial-hash) is selected via if-else based on the
         * threshold cached in Configure().
         */
        void Record(vk::CommandBuffer cb);

        bool IsInitialized() const noexcept;

        uint32_t GetMaxPairs() const noexcept;

        BroadDetectorOutputBuffers GetResultBuffers() const noexcept;

    private:
        uint32_t m_frame_counter = 0; ///< Per-frame index for descriptor-set rotation
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_SPATIALHASHBROADDETECTOR_INCLUDED
