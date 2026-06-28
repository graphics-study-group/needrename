#ifndef ENGINE_PHYSICS_GPU_ALGORITHM_COMPACTUNIQUE_INCLUDED
#define ENGINE_PHYSICS_GPU_ALGORITHM_COMPACTUNIQUE_INCLUDED

#include <cstdint>
#include <memory>

namespace Engine {
    class ComputeBuffer;
    class ParallelScan;
    class RenderGraphBuilder;
    class RenderSystem;
    enum class RGBufferHandle : int32_t;

    /**
     * @brief GPU compact-unique post-processing pass.
     *
     * Given a sorted uvec2 array, CompactUnique removes adjacent duplicates
     * and produces a contiguous output of unique entries.  It owns only the
     * compute pipelines and small per-dispatch parameter buffers; all working
     * data buffers are caller-provided.
     *
     * Algorithm:
     *   1. Flag unique:  flags[i] = (i==0 || pairs[i] != pairs[i-1]) ? 1 : 0
     *                    Writes to original_flags buffer.
     *   2. Copy flags → offsets buffer (separate buffer for scan output).
     *   3. Exclusive prefix sum on offsets (via external ParallelScan, in-place).
     *   4. Scatter:      if original_flags[i] == 1, write pairs[i] to pairs[offsets[i]].
     *   5. Write total unique count to count buffer.
     *
     * The output overwrites the input pairs buffer with compact unique pairs.
     *
     * Caller-provided resources:
     *   - Pairs buffer (sorted input, compact output)
     *   - Original flags buffer (max_elem_count uints)
     *   - Flag offsets buffer (max_elem_count uints, same size as flags)
     *   - Count buffer (1 uint, host-visible)
     *   - Pair count buffer (GPU-side uint, actual count at execution time)
     *   - ParallelScan instance + its scratch buffer (for prefix sum on offsets)
     */
    class CompactUnique {
    public:
        /**
         * @brief Construct the compact-unique executor.
         *
         * @param render_system   Render system for pipeline creation.
         * @param max_elem_count  Maximum number of uvec2 pairs this instance
         *                        will ever process.
         */
        explicit CompactUnique(RenderSystem &render_system, uint32_t max_elem_count);

        ~CompactUnique();

        CompactUnique(const CompactUnique &) = delete;
        CompactUnique &operator=(const CompactUnique &) = delete;
        CompactUnique(CompactUnique &&) = delete;
        CompactUnique &operator=(CompactUnique &&) = delete;

        /**
         * @brief Required buffer size for flags or offsets arrays.
         *
         * Both original_flags and flag_offsets buffers must be at least this size.
         *
         * @param max_elem_count  Maximum number of elements.
         * @return Minimum buffer size in bytes.
         */
        static constexpr size_t GetRequiredFlagBytes(uint32_t max_elem_count) noexcept {
            return static_cast<size_t>(max_elem_count) * sizeof(uint32_t);
        }

        /**
         * @brief Required count buffer size in bytes (always 4).
         */
        static constexpr size_t GetRequiredScratchBytes() noexcept {
            return sizeof(uint32_t);
        }

        /**
         * @brief Add compact-unique passes to the render graph.
         *
         * @param builder              Render graph builder to populate.
         * @param pairs_handle         Handle for the pairs buffer (input sorted, output compact).
         * @param pairs_buf            Pairs buffer.
         * @param flags_handle         Handle for the original flags buffer.
         * @param flags_buf            Original flags buffer (receives 0/1 per element).
         * @param offsets_handle       Handle for the offsets buffer (scan output).
         * @param offsets_buf          Offsets buffer (receives exclusive prefix sum of flags).
         * @param count_handle         Handle for the unique count buffer (1 uint).
         * @param count_buf            Unique count buffer (host-visible).
         * @param scan_scratch_handle  Handle for the ParallelScan block-sums scratch.
         * @param scan_scratch_buf     ParallelScan block-sums scratch buffer.
         * @param scan                 External ParallelScan instance.
         * @param pair_count_handle    Handle for the pair count buffer (GPU-side uint, actual count at execution time).
         * @param pair_count_buf       Pair count buffer (1 uint, written by upstream passes).
         * @param elem_capacity        Buffer capacity in pairs (for dispatch sizing).
         *
         * @pre elem_capacity <= max_elem_count
         */
        void AddPasses(
            RenderGraphBuilder &builder,
            RGBufferHandle pairs_handle,
            ComputeBuffer &pairs_buf,
            RGBufferHandle flags_handle,
            ComputeBuffer &flags_buf,
            RGBufferHandle offsets_handle,
            ComputeBuffer &offsets_buf,
            RGBufferHandle count_handle,
            ComputeBuffer &count_buf,
            RGBufferHandle scan_scratch_handle,
            ComputeBuffer &scan_scratch_buf,
            ParallelScan &scan,
            RGBufferHandle pair_count_handle,
            ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        );

        bool IsInitialized() const noexcept;

        /// Get the maximum element count this instance was configured for.
        uint32_t GetMaxElemCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_GPU_ALGORITHM_COMPACTUNIQUE_INCLUDED
