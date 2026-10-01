#ifndef ENGINE_PHYSICS_GPU_ALGORITHM_PARALLELSCAN_INCLUDED
#define ENGINE_PHYSICS_GPU_ALGORITHM_PARALLELSCAN_INCLUDED

#include "../physics_export.h"

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
     * @brief GPU parallel exclusive prefix-sum (Blelloch work-efficient scan).
     *
     * ParallelScan encapsulates the compute shader, per-pass parameter
     * management, and multi-level dispatch orchestration for parallel prefix
     * sums.  It owns its block-sums storage: the recursion writes one sum per
     * block per level into a buffer the instance allocates, grows geometrically
     * and reuses across calls, so a caller supplies only the data it wants
     * scanned and needs to know nothing about the recursion.
     *
     * Each workgroup processes 512 elements (256 threads x 2 loads).
     * For N <= 512, one pass is dispatched (mode=0).
     * For N > 512, the class dispatches:
     *   1. mode=1: scan each 512-element block, write per-block sums to scratch
     *   2. mode=0: recursively scan the block-sums region (using data_offset to
     *      read from the sub-block region without aliasing data)
     *   3. add_block_offset shader: add prefix-summed block offsets back to data
     *
     * Input and output buffers are always separate bindings.  The caller may
     * pass the same Rhi::ComputeBuffer for both to achieve in-place scan.
     *
     * The instance holds no geometry: a call supplies its own element count, so
     * one instance serves any number of scans within a single frame.  Because
     * the block-sums storage is shared by every call, two `Record` calls on one
     * instance in the same command buffer have an execution-order dependency
     * through it and the caller must record a barrier between them.
     */
    class PHYSICS_API ParallelScan {
    public:
        /**
         * @brief Construct the parallel scan executor.
         *
         * Allocates no GPU resources; the pipeline and the block-sums storage
         * are created lazily on the first `Record` call.
         *
         * @param device_context  Device context for pipeline creation.
         */
        explicit ParallelScan(Rhi::DeviceContext &device_context);

        ~ParallelScan();

        ParallelScan(const ParallelScan &) = delete;
        ParallelScan &operator=(const ParallelScan &) = delete;
        ParallelScan(ParallelScan &&) = delete;
        ParallelScan &operator=(ParallelScan &&) = delete;

        /**
         * @brief Record scan dispatches to the command buffer.
         *
         * Performs an exclusive prefix sum over @p input_buf and writes the
         * result to @p output_buf.  The two buffers may alias (in-place scan).
         *
         * Inserts a MemoryBarrier2 at the start and between internal passes.
         *
         * @param cb           Command buffer in recording state.
         * @param input_buf    Input data buffer (uint elements).
         * @param output_buf   Output data buffer (uint elements).
         * @param elem_count   Number of uint elements to scan.  Zero records
         *                     nothing.
         */
        void Record(
            vk::CommandBuffer cb, Rhi::ComputeBuffer &input_buf, Rhi::ComputeBuffer &output_buf, uint32_t elem_count
        );

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_GPU_ALGORITHM_PARALLELSCAN_INCLUDED
