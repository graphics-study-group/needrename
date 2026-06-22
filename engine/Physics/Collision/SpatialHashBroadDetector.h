#ifndef ENGINE_PHYSICS_SPATIALHASHBROADDETECTOR_INCLUDED
#define ENGINE_PHYSICS_SPATIALHASHBROADDETECTOR_INCLUDED

#include <glm.hpp>
#include <memory>

namespace Engine {
    class ComputeBuffer;
    class PhysicsScene;
    class RenderGraphBuilder;
    class RenderSystem;

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
     * @brief GPU spatial-hash broad-phase collision detector.
     *
     * SpatialHashBroadDetector replaces the all-pairs pair-generation approach
     * with a spatial-hash filter: per-shape AABBs are computed, shapes are
     * assigned to a uniform 3D grid via a two-pass scheme, (cell_id, shape_id)
     * pairs are sorted by cell ID using GPU counting sort, and candidate
     * collision pairs are generated only for shapes sharing at least one cell.
     *
     * Large shapes spanning more than max_cells_per_shape cells, and shapes
     * outside world bounds, are marked "global" and paired with all other
     * shapes via a separate all-pairs pass.
     *
     * When shape_count <= fallback_all_pairs_threshold, the entire spatial-hash
     * pipeline is skipped in favour of direct all-pairs generation.
     *
     * Owned GPU buffers:
     *   - aabb_min[] / aabb_max[]       — per-shape AABBs (vec4)
     *   - shape_cell_count[]            — per-shape cell overlap count (uint)
     *   - shape_cell_offset[]           — per-shape write offset (uint, scan of count)
     *   - cell_shape_pairs[]            — (cell_id, shape_id) pairs (uvec2)
     *   - sorted_pairs[]                — sorted copy of cell_shape_pairs (uvec2)
     *   - cell_histogram[]              — per-cell shape count (uint)
     *   - cell_offsets[]                — per-cell start offset (uint, scan of histogram)
     *   - cell_scratch[]                — per-cell atomic counter for scatter (uint)
     *   - global_flags[]                — per-shape global marker (uint)
     *   - total_assignments             — single uint: total (cell, shape) entries
     *   - collision_pairs[]             — output: candidate pairs (uvec2)
     *   - pair_count                    — output: actual pair count (uint)
     *   - grid_config_ubo               — grid parameters uniform (GridConfigGpu)
     *
     * Multi-level parallel_scan orchestration is handled internally by the
     * DispatchParallelScan helper.
     */
    class SpatialHashBroadDetector {
    public:
        /**
         * @brief Construct the broad-phase detector.
         *
         * @param render_system              Render system for pipeline creation.
         * @param grid_config                Spatial hash grid configuration.
         * @param fallback_all_pairs_threshold Shape count below which all-pairs
         *                                     fallback is used instead of spatial
         *                                     hashing.
         */
        explicit SpatialHashBroadDetector(
            RenderSystem &render_system,
            const GridConfig &grid_config,
            uint32_t fallback_all_pairs_threshold = 32
        );

        ~SpatialHashBroadDetector();

        SpatialHashBroadDetector(const SpatialHashBroadDetector &) = delete;
        SpatialHashBroadDetector &operator=(const SpatialHashBroadDetector &) = delete;
        SpatialHashBroadDetector(SpatialHashBroadDetector &&) = delete;
        SpatialHashBroadDetector &operator=(SpatialHashBroadDetector &&) = delete;

        /**
         * @brief Fill a render graph builder with broad-phase compute passes.
         *
         * @param builder       Render graph builder to populate.
         * @param physics_scene Physics scene providing GPU shape buffers.
         */
        void Step(RenderGraphBuilder &builder, PhysicsScene &physics_scene);

        bool IsInitialized() const noexcept;

        /**
         * @brief Get the collision pair buffer (uvec2[], written each frame).
         */
        const ComputeBuffer &GetPairBuffer() const noexcept;

        /**
         * @brief Get the pair count buffer (single uint, written each frame).
         */
        const ComputeBuffer &GetPairCountBuffer() const noexcept;

        /**
         * @brief Get the maximum number of pairs the output buffer can hold.
         */
        uint32_t GetMaxPairs() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_SPATIALHASHBROADDETECTOR_INCLUDED
