#ifndef ENGINE_PHYSICS_SPATIALHASHBROADDETECTOR_INCLUDED
#define ENGINE_PHYSICS_SPATIALHASHBROADDETECTOR_INCLUDED

#include <glm.hpp>
#include <memory>

namespace vk {
    struct CommandBuffer;
} // namespace vk

namespace Engine {
    class ComputeBuffer;
    class PhysicsScene;
    class RenderSystem;
    enum class RGBufferHandle : int32_t;

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
     * Returned by Detect().  References are guaranteed valid for the detector's
     * lifetime.
     */
    struct BroadDetectorOutputBuffers {
        const ComputeBuffer &pair_buffer;
        const ComputeBuffer &pair_count_buffer;
        uint32_t max_pairs;
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
     *   1. Construct with RenderSystem& only (no GPU allocation).
     *   2. Configure(scene, shape_count, grid_config, threshold) — CPU prep.
     *   3. Detect(cb) — lazy-build RG, record passes to cb, return output buffers.
     */
    class SpatialHashBroadDetector {
    public:
        explicit SpatialHashBroadDetector(RenderSystem &render_system);
        ~SpatialHashBroadDetector();

        SpatialHashBroadDetector(const SpatialHashBroadDetector &) = delete;
        SpatialHashBroadDetector &operator=(const SpatialHashBroadDetector &) = delete;
        SpatialHashBroadDetector(SpatialHashBroadDetector &&) = delete;
        SpatialHashBroadDetector &operator=(SpatialHashBroadDetector &&) = delete;

        /**
         * @brief CPU-side preparation: cache references, size buffers, upload config.
         *
         * Safe to call every frame — no-op when nothing changed.
         *
         * @param scene                        Physics scene for GPU buffer access.
         * @param shape_count                  Number of shapes in the scene.
         * @param grid_config                  Spatial hash grid configuration.
         * @param fallback_all_pairs_threshold  Shape count below which all-pairs
         *                                      fallback is used instead of spatial
         *                                      hashing.
         */
        void Configure(
            PhysicsScene &scene,
            uint32_t shape_count,
            const GridConfig &grid_config,
            uint32_t fallback_all_pairs_threshold
        );

        /**
         * @brief GPU-side: lazy-build RenderGraph, record passes to cb.
         *
         * Must be called after Configure().  The detector owns its RG and
         * rebuilds it only when sizing parameters change.  The RG structure
         * is chosen at build time (fallback vs spatial-hash).
         *
         * @return Raw pointers to output buffers (pair buffer, pair count).
         */
        BroadDetectorOutputBuffers Detect(vk::CommandBuffer cb);

        bool IsInitialized() const noexcept;

        /**
         * @brief Get max_pairs (output pair buffer capacity).
         */
        uint32_t GetMaxPairs() const noexcept;

        /**
         * @brief Get raw pointers to owned pair output buffers.
         *
         * Valid after Configure().  Pointers are stable for the detector's lifetime.
         */
        const ComputeBuffer *GetPairBuffer() const noexcept;
        const ComputeBuffer *GetPairCountBuffer() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_SPATIALHASHBROADDETECTOR_INCLUDED
