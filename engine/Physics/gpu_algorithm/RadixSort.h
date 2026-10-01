#ifndef ENGINE_PHYSICS_GPU_ALGORITHM_RADIXSORT_INCLUDED
#define ENGINE_PHYSICS_GPU_ALGORITHM_RADIXSORT_INCLUDED

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
     * @brief The caller's buffers for one radix sort call.
     *
     * The record is a struct-of-arrays: a `uint` **key** array plus an
     * **optional** `uint` payload array of the same capacity.  There is no packed
     * pair and no mode selector: the key array is the ordering key, and the
     * payload array is opaque data that is permuted together with its key.
     *
     * The key array is both the input and the output: `Record` sorts it in place,
     * permuting the payload array alongside it.  The sort's ping-pong partner
     * arrays are its own, so a caller allocates no temporary storage and does not
     * have to know which array the result ended up in.
     *
     * Callers that bind no payload set `payload` to `nullptr`.  A caller whose
     * record *is* its key (for example a pair identity packed into a single
     * `uint`) has nothing to permute.
     */
    struct RadixSortBuffers {
        Rhi::ComputeBuffer *keys{nullptr};
        Rhi::ComputeBuffer *payload{nullptr};
        Rhi::ComputeBuffer *count{nullptr};
    };

    /**
     * @brief GPU 8-bit LSD radix sort over a `uint` key array and an optional
     * `uint` payload array.
     *
     * **Result.** The sort returns the caller's keys in **stable ascending**
     * order: for any two elements `i < j` with equal keys, the element at `i`
     * appears before the element at `j` in the output.  Stability holds for every
     * pass count, because LSD radix sort is only correct when each pass preserves
     * the relative order the previous one established.  One `Record` call sorts
     * one array; the payload array, when present, is permuted by the same
     * permutation the keys receive.  The result is always in the caller's arrays,
     * whatever the pass count: when the pass count is odd the last pass leaves it
     * in the instance's own array, and `Record` copies it back.  A later call on
     * the same instance therefore cannot destroy an earlier call's result.
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
     *
     *   2. **Prefix sum** (the internal `ParallelScan`): one flat in-place
     *      exclusive scan over the whole `256 * num_blocks`-element transposed
     *      histogram.  Because the digit is the outer dimension and the block the
     *      inner one, the scanned value at `digit * num_blocks + block` is exactly
     *      that digit's global base offset plus its counts in all earlier blocks.
     *
     *   3. **Stable scatter** (`radix_scatter.comp`): each element's destination
     *      is the scanned offset for its `(digit, block)` pair plus its
     *      deterministic in-block rank.  No atomic operation decides any
     *      element's order.
     *
     * The instance holds no geometry: every `Record` call supplies its own
     * capacity and key bound, so one instance serves any number of them within a
     * single frame and never needs rebuilding when the caller's geometry changes.
     * The working storage is shared by every call, so two `Record` calls on one
     * instance in the same command buffer have an execution-order dependency
     * through it and the caller must record a barrier between them.
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
         * loading, kernel acquisition and working-storage allocation are deferred
         * until the first `Record` call.
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
         * @brief Record the radix sort's dispatches to the command buffer.
         *
         * Sorts the first `elem_capacity` elements of `buffers.keys` into
         * ascending stable key order, in place, permuting `buffers.payload` in
         * lockstep when one is supplied.  Inserts the compute barriers its
         * sub-steps, passes and result copy need.
         *
         * A capacity of zero, or a key bound that implies no passes, records
         * nothing: the caller's key array is already the sorted result.
         *
         * @param cb              Command buffer in recording state.
         * @param buffers         The caller's key array, optional payload array
         *                        and element-count buffer.
         * @param elem_capacity   Element capacity the buffers are sized for, and
         *                        the count of elements this call sorts.  Dispatch
         *                        geometry comes from this value, never from the
         *                        GPU-side count.
         * @param max_key_value   A value at or above every key the call will
         *                        write, used to derive the pass count.
         *
         * @pre The key array is at least `elem_capacity * sizeof(uint32_t)`
         *      bytes, the payload array (when supplied) is at least that large,
         *      and the count buffer holds one `uint`.
         * @pre Every key the caller writes is below `2^(8 * num_passes)`.
         *
         * @throws std::invalid_argument if `max_key_value` is zero.
         * @throws std::runtime_error if a required buffer is null or smaller than
         *         the size this call's geometry implies.
         */
        void Record(
            vk::CommandBuffer cb, const RadixSortBuffers &buffers, uint32_t elem_capacity, uint32_t max_key_value
        );

        bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_GPU_ALGORITHM_RADIXSORT_INCLUDED
