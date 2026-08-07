#ifndef ENGINE_PHYSICS_GPU_ALGORITHM_RADIXSORT_INCLUDED
#define ENGINE_PHYSICS_GPU_ALGORITHM_RADIXSORT_INCLUDED

#include <cstdint>
#include <memory>

namespace vk {
    class CommandBuffer;
}
namespace Engine {
    namespace Rhi {
        class ComputeBuffer;
    }
    namespace Rhi {
        class DeviceContext;
    }

    /**
     * @brief GPU 8-bit LSD radix sort for uvec2 pairs.
     *
     * RadixSort encapsulates the compute shaders and per-pass parameter
     * management for sorting uvec2(a, b) pairs by (primary, secondary) key.
     * It owns only the compute pipelines and small per-dispatch parameter
     * buffers; all working data buffers are caller-provided.
     *
     * Algorithm: 8-pass 8-bit LSD radix sort.
     *   Passes 0-3 sort by .y (secondary key, word_select=0).
     *   Passes 4-7 sort by .x (primary key,   word_select=1).
     *
     * Each radix pass consists of three sub-steps:
     *   1. Histogram: count elements per digit (0-255)
     *   2. Prefix sum: exclusive scan over 256 histogram entries (single WG)
     *   3. Scatter:   reorder elements using histogram as atomic counters
     *
     * A clear (memset) precedes each histogram pass.  Data ping-pongs between
     * two pair buffers; after 8 passes the final sorted result lands in the
     * original pairs_buf_a.
     *
     * Max shape count is 2^20 (1,048,576).  Exceeding this limit throws.
     *
     * Caller-provided resources:
     *   - Input/output pair buffer A (ping)
     *   - Temp pair buffer B (pong, same size as A)
     *   - Scratch buffer (256 uints, 1 KB)
     *   - elem_capacity (buffer size in pairs, for dispatch sizing)
     *   - pair_count buffer (GPU-side uint, actual pair count at execution time)
     *   - max_shape_count (for validation)
     */
    class RadixSort {
    public:
        static constexpr uint32_t kNumBins = 256;
        static constexpr uint32_t kMaxShapeCount = 1u << 20; // 1,048,576
        static constexpr uint32_t kNumPasses = 8;            // 4 for b + 4 for a

        /**
         * @brief Construct the radix sort executor.
         *
         * @param render_system   Render system for pipeline creation.
         * @param max_elem_count  Maximum number of uvec2 pairs this instance
         *                        will ever sort.  Used for dispatch bounds.
         */
        explicit RadixSort(Rhi::DeviceContext &device_context, uint32_t max_elem_count);

        ~RadixSort();

        RadixSort(const RadixSort &) = delete;
        RadixSort &operator=(const RadixSort &) = delete;
        RadixSort(RadixSort &&) = delete;
        RadixSort &operator=(RadixSort &&) = delete;

        /**
         * @brief Required scratch buffer size in bytes (256-element histogram).
         *
         * @return Always 1024 bytes (256 × sizeof(uint32_t)).
         */
        static constexpr size_t GetRequiredScratchBytes() noexcept {
            return kNumBins * sizeof(uint32_t);
        }

        /**
         * @brief Required temp pairs buffer size in bytes.
         *
         * The temp buffer must be at least as large as the input pair buffer
         * to support ping-pong between passes.
         *
         * @param max_elem_count  Maximum number of uvec2 pairs to sort.
         * @return Minimum buffer size in bytes.
         */
        static constexpr size_t GetRequiredTempPairsBytes(uint32_t max_elem_count) noexcept {
            return static_cast<size_t>(max_elem_count) * 2u * sizeof(uint32_t);
        }

        /**
         * @brief Record 8-pass radix sort dispatches to the command buffer.
         *
         * Sorts uvec2 pairs by (.x, .y) ascending.  After execution the sorted
         * result is in @p pairs_buf_a.
         *
         * Inserts barriers between internal passes.
         *
         * @param cb               Command buffer in recording state.
         * @param pairs_buf_a      Ping pairs buffer (input, becomes output).
         * @param pairs_buf_b      Pong pairs buffer (temp, same size).
         * @param scratch_buf      Histogram scratch buffer (>= 1 KB).
         * @param elem_capacity    Buffer capacity in pairs (for dispatch sizing).
         * @param pair_count_buf   Pair count buffer (1 uint, read at GPU execution time).
         * @param max_shape_count  Max shape index, for validation (must <= 2^20).
         *
         * @pre elem_capacity <= max_elem_count
         * @throws std::runtime_error if max_shape_count > kMaxShapeCount
         */
        void Record(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &pairs_buf_a,
            Rhi::ComputeBuffer &pairs_buf_b,
            Rhi::ComputeBuffer &scratch_buf,
            uint32_t elem_capacity,
            Rhi::ComputeBuffer &pair_count_buf,
            uint32_t max_shape_count
        );

        bool IsInitialized() const noexcept;

        /// Get the maximum element count this instance was configured for.
        uint32_t GetMaxElemCount() const noexcept;

        /**
         * @brief Reset the per-pass parameter buffer pool.
         *
         * Frees all allocated parameter buffers so they can be reused in
         * subsequent Record calls.  This is typically called after a
         * Record operation completes to ensure fresh allocation for the next
         * dispatch.
         */
        void ResetParamPool();

    private:
        uint32_t m_frame_counter = 0; ///< Per-frame index for descriptor-set rotation
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_GPU_ALGORITHM_RADIXSORT_INCLUDED
