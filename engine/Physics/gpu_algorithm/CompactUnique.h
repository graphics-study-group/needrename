#ifndef ENGINE_PHYSICS_GPU_ALGORITHM_COMPACTUNIQUE_INCLUDED
#define ENGINE_PHYSICS_GPU_ALGORITHM_COMPACTUNIQUE_INCLUDED

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
     * @brief GPU compact-unique post-processing pass over a sorted `uint` key
     * array.
     *
     * Given a sorted `uint` array, CompactUnique removes adjacent duplicates
     * and produces a contiguous output of unique entries.  It owns its compute
     * pipelines and its working storage — the unique-flag and flag-offset arrays
     * and a private `ParallelScan` with its own block sums, all grown
     * geometrically and reused across calls — so a caller supplies only its data.
     *
     * The algorithm knows nothing about what a key encodes: it never decodes,
     * reconstructs or re-encodes a record.  A caller whose record is not a scalar
     * packs the record identity into the key itself and restores it after
     * compaction (the broad phase packs `a * shape_count + b` and unpacks in its
     * own pass).
     *
     * Algorithm:
     *
     *   1. Flag unique:  flags[i] = (i==0 || keys[i] != keys[i-1]) ? 1 : 0
     *                     Writes to original_flags buffer.
     *
     *   2. Copy flags -> offsets buffer (separate buffer for scan output).
     *
     *   3. Exclusive prefix sum on offsets (via the internal ParallelScan,
     *      in-place).
     *
     *   4. Scatter:      if original_flags[i] == 1, write keys[i] to keys[offsets[i]].
     *
     *   5. Write total unique count to count buffer.
     *
     * The output overwrites the input key buffer with the compact unique keys.
     *
     * The instance holds no geometry: a call supplies its own element capacity,
     * so one instance serves any number of calls within a single frame.  Because
     * the working storage is shared by every call, two `Record` calls on one
     * instance in the same command buffer have an execution-order dependency
     * through it and the caller must record a barrier between them.
     *
     * Caller-provided resources:
     *
     *   - Key buffer (sorted input, compact output)
     *
     *   - Count buffer (1 uint, receives the unique count)
     *
     *   - Element count buffer (GPU-side uint, actual count at execution time)
     */
    class PHYSICS_API CompactUnique {
    public:
        /**
         * @brief Construct the compact-unique executor.
         *
         * Allocates no GPU resources and stores no geometry; shader loading,
         * kernel acquisition and working-storage allocation are deferred until
         * the first `Record` call.
         *
         * @param device_context  Device context for pipeline creation.
         */
        explicit CompactUnique(Rhi::DeviceContext &device_context);

        ~CompactUnique();

        CompactUnique(const CompactUnique &) = delete;
        CompactUnique &operator=(const CompactUnique &) = delete;
        CompactUnique(CompactUnique &&) = delete;
        CompactUnique &operator=(CompactUnique &&) = delete;

        /**
         * @brief Record compact-unique dispatches to the command buffer.
         *
         * @param cb               Command buffer in recording state.
         * @param keys_buf         Key buffer (sorted input, compact output).
         * @param count_buf        Unique count buffer (1 uint, receives the count).
         * @param elem_count_buf   Element count buffer (1 uint, written by upstream
         *                         passes).
         * @param elem_capacity    Buffer capacity in keys (for dispatch sizing).
         *                         Zero records nothing.
         */
        void Record(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &keys_buf,
            Rhi::ComputeBuffer &count_buf,
            Rhi::ComputeBuffer &elem_count_buf,
            uint32_t elem_capacity
        );

        bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_GPU_ALGORITHM_COMPACTUNIQUE_INCLUDED
