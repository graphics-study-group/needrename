#ifndef ENGINE_PHYSICS_GPU_ALGORITHM_PARALLELSCAN_INCLUDED
#define ENGINE_PHYSICS_GPU_ALGORITHM_PARALLELSCAN_INCLUDED

#include <memory>
#include <cstdint>

namespace Engine {
    class ComputeBuffer;
    class RenderGraphBuilder;
    class RenderSystem;
    enum class RGBufferHandle : int32_t;

    /**
     * @brief GPU parallel exclusive prefix-sum (Blelloch work-efficient scan).
     *
     * ParallelScan encapsulates the compute shader, per-pass parameter
     * management, and multi-level dispatch orchestration for parallel prefix
     * sums.  It owns only the compute pipeline; all data and scratch buffers
     * are caller-provided so that the caller can import them once into the
     * render graph and reuse the same handle across multiple scan operations.
     *
     * The shader processes 512 elements per workgroup (256 threads × 2 loads).
     * For N ≤ 512, one pass is dispatched.  For N > 512, the class dispatches:
     *   1. mode=1: scan each 512-element block, write per-block sums to scratch
     *   2. mode=0: recursively scan the scratch buffer
     *   3. mode=2: add scanned block offsets back to the data buffer
     *
     * Input and output buffers are always separate bindings.  The caller may
     * pass the same ComputeBuffer for both to achieve in-place scan.
     *
     * Scratch buffer sizing:
     *   Use GetRequiredBlockSumsBytes(max_elem_count) to determine the minimum
     *   size.  The scratch buffer holds ceil(N/512) uint32_t entries for the
     *   root-level block totals; deeper recursion levels reuse the same buffer
     *   region (sub-block totals temporarily overwrite the first few entries
     *   and are restored by the mode-2 add-back pass).
     *
     * Owned GPU resources:
     *   - Scan compute stage (shared across all dispatches)
     *   - Per-pass parameter buffer pool
     *
     * Caller-provided resources:
     *   - Input / output data buffers
     *   - Block-sums scratch buffer (sized via GetRequiredBlockSumsBytes)
     */
    class ParallelScan {
    public:
        /**
         * @brief Construct the parallel scan executor.
         *
         * @param render_system   Render system for pipeline creation.
         * @param max_elem_count  Maximum number of elements this instance will
         *                        ever scan.  Used for bounds-checking in
         *                        AddPasses and sizing via GetRequiredBlockSumsBytes.
         */
        explicit ParallelScan(RenderSystem &render_system, uint32_t max_elem_count);

        ~ParallelScan();

        ParallelScan(const ParallelScan &) = delete;
        ParallelScan &operator=(const ParallelScan &) = delete;
        ParallelScan(ParallelScan &&) = delete;
        ParallelScan &operator=(ParallelScan &&) = delete;

        /**
         * @brief Compute the minimum block-sums scratch buffer size in bytes.
         *
         * The caller allocates a buffer of at least this size (or larger) and
         * imports it into the render graph once.  The same buffer and handle
         * may be reused across multiple AddPasses calls.
         *
         * @param max_elem_count  Maximum number of uint elements to scan.
         * @return Minimum buffer size in bytes (= ceil(max_elem_count/512) × 4).
         */
        static size_t GetRequiredBlockSumsBytes(uint32_t max_elem_count) noexcept;

        /**
         * @brief Add scan passes to the render graph.
         *
         * Performs an exclusive prefix sum over @p input_buf and writes the
         * result to @p output_buf.  The two buffers may alias (in-place scan).
         *
         * @param builder            Render graph builder to populate.
         * @param input_handle       Pre-imported handle for the input data buffer.
         * @param output_handle      Pre-imported handle for the output data buffer.
         * @param input_buf          Input data buffer (uint elements).
         * @param output_buf         Output data buffer (uint elements).
         * @param block_sums_handle  Pre-imported handle for the block-sums scratch
         *                           buffer.  The caller should import this once and
         *                           reuse the same handle across all AddPasses calls
         *                           to ensure correct render-graph dependency tracking.
         * @param block_sums_buf     Block-sums scratch buffer (uint elements).
         *                           Must be at least GetRequiredBlockSumsBytes(max_elem_count)
         *                           bytes in size.
         * @param elem_count         Number of uint elements to scan.
         *
         * @pre elem_count > 0 && elem_count <= max_elem_count
         */
        void AddPasses(
            RenderGraphBuilder &builder,
            RGBufferHandle input_handle,
            RGBufferHandle output_handle,
            ComputeBuffer &input_buf,
            ComputeBuffer &output_buf,
            RGBufferHandle block_sums_handle,
            ComputeBuffer &block_sums_buf,
            uint32_t elem_count
        );

        bool IsInitialized() const noexcept;

        /// Get the maximum element count this instance was configured for.
        uint32_t GetMaxElemCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_GPU_ALGORITHM_PARALLELSCAN_INCLUDED
