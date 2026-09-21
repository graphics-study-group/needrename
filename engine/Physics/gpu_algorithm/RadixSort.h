#ifndef ENGINE_PHYSICS_GPU_ALGORITHM_RADIXSORT_INCLUDED
#define ENGINE_PHYSICS_GPU_ALGORITHM_RADIXSORT_INCLUDED

#include "ParallelScan.h"

#include "../physics_export.h"

#include <cstddef>
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
     * @brief The caller's buffers for one radix sort call.
     *
     * The record is a struct-of-arrays: a `uint` **key** array plus an
     * **optional** `uint` payload array of the same capacity.  There is no packed
     * pair and no mode selector: the key array is the ordering key, and the
     * payload array is opaque data that is permuted together with its key.
     *
     * The two key arrays are the sort's ping-pong pair; `keys_a` is the input of
     * the first pass.  Each pass writes into the array it did not read, so the
     * array holding the result depends on how many passes ran — the sort returns
     * that choice from `Record` rather than leaving it to the caller.
     *
     * Callers that bind no payload set both payload entries to `nullptr`.  A
     * caller whose record *is* its key (for example a pair identity packed into a
     * single `uint`) has nothing to permute.
     */
    struct RadixSortBuffers {
        Rhi::ComputeBuffer *keys_a{nullptr};
        Rhi::ComputeBuffer *keys_b{nullptr};
        Rhi::ComputeBuffer *payload_a{nullptr};
        Rhi::ComputeBuffer *payload_b{nullptr};
        Rhi::ComputeBuffer *scratch{nullptr};
        Rhi::ComputeBuffer *count{nullptr};
    };

    /**
     * @brief The buffers holding a completed sort's result.
     *
     * Which of the caller's two arrays holds the result depends on the pass
     * count, so the sort reports it.  `payload` is `nullptr` when the call had no
     * payload array.
     */
    struct RadixSortOutput {
        Rhi::ComputeBuffer *keys{nullptr};
        Rhi::ComputeBuffer *payload{nullptr};
    };

    /**
     * @brief GPU 8-bit LSD radix sort over a `uint` key array and an optional
     * `uint` payload array.
     *
     * RadixSort encapsulates the compute shaders and the per-pass constants of a
     * least-significant-digit radix sort.  It owns only the compute pipelines and
     * an internal `ParallelScan`; every data buffer is caller-provided, and the
     * instance holds no geometry.
     *
     * **Result.** The sort returns the caller's keys in **stable ascending**
     * order: for any two elements `i < j` with equal keys, the element at `i`
     * appears before the element at `j` in the output.  Stability holds for every
     * pass count, because LSD radix sort is only correct when each pass preserves
     * the relative order the previous one established.  One `Record` call sorts
     * one array; the payload array, when present, is permuted by the same
     * permutation the keys receive.
     *
     * **Pass count.** The number of passes is derived from the caller's key
     * bound, not fixed:
     *
     * ```
     * num_passes = ceil(bit_width(max_key_value) / 8)
     * ```
     *
     * so a bound that fits in one byte costs one pass instead of eight.  A bound
     * of one or zero costs no passes at all, and the call leaves the caller's
     * buffers untouched.  Each pass consists of exactly three sub-steps, with no
     * clear pass:
     *
     *   1. **Per-block histogram** (`radix_block_histogram.comp`): each block of
     *      256 elements counts its own digits and writes the block's 256 counts
     *      **transposed** into the scratch buffer, so
     *      `hist[digit * num_blocks + block]` is the count of that digit in that
     *      block.  Every cell is written by exactly one invocation on every call,
     *      which is why no clear pass is needed.
     *   2. **Prefix sum** (the internal `ParallelScan`): one flat in-place
     *      exclusive scan over the whole `256 * num_blocks`-element transposed
     *      histogram.  Because the digit is the outer dimension and the block the
     *      inner one, the scanned value at `digit * num_blocks + block` is exactly
     *      that digit's global base offset plus its counts in all earlier blocks.
     *   3. **Stable scatter** (`radix_scatter.comp`): each element's destination
     *      is the scanned offset for its `(digit, block)` pair plus its
     *      deterministic in-block rank.  No atomic operation decides any
     *      element's order.
     *
     * **Key bound contract.** `max_key_value` must be at or above every key the
     * caller will write, and the sort will **not** clamp, mask or otherwise
     * repair a key that exceeds it: a clamp would merge out-of-range keys into
     * the top bin, where their mutual order is arbitrary and unrecoverable, and
     * the result would be a plausible but non-ascending array with no error.  The
     * contract is stated as *every key is below `2^(8 * num_passes)`*, so a key
     * exactly equal to `max_key_value` stays valid.
     *
     * Verifying the bound is the **caller's** job, on the host, because only the
     * caller knows its key domain.  Each call site must assert that the keys it
     * is about to produce fit the bound it passes.
     *
     * **Scratch.** The caller passes one scratch buffer sized by
     * `GetRequiredScratchBytes(max_elem_capacity)`; the sort partitions it
     * internally into the transposed histogram and the scan's block sums, and the
     * instance rebuilds its internal `ParallelScan` when a call's element
     * capacity exceeds the one it was built for.  The sort allocates no buffer.
     *
     * The instance holds no geometry: every `Record` call supplies its own
     * capacity and key bound, so one instance serves any number of them within a
     * single frame and never needs rebuilding when the caller's geometry changes.
     */
    class PHYSICS_API RadixSort {
    public:
        static constexpr uint32_t kNumBins = 256u;
        /// Elements per histogram/scatter workgroup: one block of the transposed
        /// histogram.
        static constexpr uint32_t kBlockSize = 256u;

        /**
         * @brief Construct the radix sort executor.
         *
         * Allocates no GPU resources and stores no element geometry; shader
         * loading and `ComputeStage` instantiation are deferred until the first
         * `Record` call.
         *
         * @param device_context  Device context for pipeline creation.
         */
        explicit RadixSort(Rhi::DeviceContext &device_context);

        ~RadixSort();

        RadixSort(const RadixSort &) = delete;
        RadixSort &operator=(const RadixSort &) = delete;
        RadixSort(RadixSort &&) = delete;
        RadixSort &operator=(RadixSort &&) = delete;

        /**
         * @brief Required scratch buffer size in bytes.
         *
         * The scratch is exactly `256 * ceil(max_elem_capacity / 256)` `uint`s for
         * the transposed per-block digit histogram, followed by
         * `ParallelScan::GetRequiredBlockSumsBytes` of the same element count for
         * the scan's block sums.  The class partitions the buffer internally and
         * does not expose the partition.
         *
         * @param max_elem_capacity  Largest element capacity the caller will ever
         *                           pass to `Record`.
         * @return Minimum scratch buffer size in bytes.
         */
        static size_t GetRequiredScratchBytes(uint32_t max_elem_capacity) noexcept;

        /**
         * @brief Required size in bytes of **each** ping-pong temporary array.
         *
         * One temporary key array is always needed, and one more temporary
         * payload array is needed when the caller supplies a payload array.
         *
         * @param max_elem_capacity  Largest element capacity the caller will ever
         *                           pass to `Record`.
         * @return Minimum buffer size in bytes for one temporary array.
         */
        static size_t GetRequiredTempBytes(uint32_t max_elem_capacity) noexcept;

        /**
         * @brief Record the radix sort's dispatches to the command buffer.
         *
         * Sorts the first `elem_capacity` elements of `buffers.keys_a` — or of
         * whichever array the call's ping-pong parity makes its input — into
         * ascending stable key order, permuting the payload array in lockstep.
         * Inserts the compute barriers its sub-steps and passes need.
         *
         * A capacity of zero, or a key bound that implies no passes, records
         * nothing and returns the caller's input key array (and input payload
         * array) as the result.
         *
         * @param cb              Command buffer in recording state.
         * @param buffers         The caller's key arrays, optional payload arrays,
         *                        scratch and element-count buffer.
         * @param elem_capacity   Element capacity the buffers are sized for, and
         *                        the count of elements this call sorts.  Dispatch
         *                        geometry comes from this value, never from the
         *                        GPU-side count.
         * @param max_key_value   A value at or above every key the call will
         *                        write, used to derive the pass count.
         *
         * @return The buffers holding the sorted result.
         *
         * @pre The key arrays are at least `elem_capacity * sizeof(uint32_t)`
         *      bytes each, the payload arrays (when supplied) are at least that
         *      large, the scratch buffer is at least
         *      `GetRequiredScratchBytes(elem_capacity)` bytes, and the count
         *      buffer holds one `uint`.
         * @pre Every key the caller writes is below `2^(8 * num_passes)`.
         *
         * @throws std::invalid_argument if `max_key_value` is zero, or if only one
         *         of the two payload arrays is supplied.
         * @throws std::runtime_error if a required buffer is null or smaller than
         *         the size this call's geometry implies.
         */
        RadixSortOutput Record(
            vk::CommandBuffer cb,
            const RadixSortBuffers &buffers,
            uint32_t elem_capacity,
            uint32_t max_key_value
        );

        bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_GPU_ALGORITHM_RADIXSORT_INCLUDED
