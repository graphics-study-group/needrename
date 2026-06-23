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
     * sums.  Consumers call AddPasses() once per scan operation; the class
     * internally determines whether a single workgroup suffices or a multi-level
     * three-pass scan is required.
     *
     * The shader processes 512 elements per workgroup (256 threads × 2 loads).
     * For N ≤ 512, one pass is dispatched.  For N > 512, the class dispatches:
     *   1. mode=1: scan each 512-element block, write per-block sums
     *   2. mode=0: recursively scan the block-sums buffer
     *   3. mode=2: add scanned block offsets back to the data buffer
     *
     * Input and output buffers are always separate bindings.  The caller may
     * pass the same ComputeBuffer for both to achieve in-place scan.
     *
     * Owned GPU resources:
     *   - Scan compute stage (shared across all dispatches)
     *   - Block-sums scratch buffer (sized for max_elem_count)
     *   - Per-pass parameter buffer pool
     */
    class ParallelScan {
    public:
        /**
         * @brief Construct the parallel scan executor.
         *
         * @param render_system   Render system for pipeline creation.
         * @param max_elem_count  Maximum number of elements this instance will
         *                        ever scan.  Determines the scratch buffer size.
         */
        explicit ParallelScan(RenderSystem &render_system, uint32_t max_elem_count);

        ~ParallelScan();

        ParallelScan(const ParallelScan &) = delete;
        ParallelScan &operator=(const ParallelScan &) = delete;
        ParallelScan(ParallelScan &&) = delete;
        ParallelScan &operator=(ParallelScan &&) = delete;

        /**
         * @brief Add scan passes to the render graph.
         *
         * Performs an exclusive prefix sum over @p input_buf and writes the
         * result to @p output_buf.  The two buffers may alias (in-place scan).
         *
         * @param builder        Render graph builder to populate.
         * @param input_handle   Pre-imported handle for the input data buffer.
         * @param output_handle  Pre-imported handle for the output data buffer.
         *                       If input_handle == output_handle, the pass is
         *                       declared with read+write access for in-place scan.
         * @param input_buf      Input data buffer (uint elements).
         * @param output_buf     Output data buffer (uint elements).
         * @param elem_count     Number of uint elements to scan.
         *
         * @pre elem_count > 0 && elem_count <= max_elem_count
         */
        void AddPasses(
            RenderGraphBuilder &builder,
            RGBufferHandle input_handle,
            RGBufferHandle output_handle,
            ComputeBuffer &input_buf,
            ComputeBuffer &output_buf,
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
