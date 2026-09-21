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
    class ParallelScan;
    namespace Rhi {
        class DeviceContext;
    }

    /**
     * @brief GPU compact-unique post-processing pass over a sorted `uint` key
     * array.
     *
     * Given a sorted `uint` array, CompactUnique removes adjacent duplicates
     * and produces a contiguous output of unique entries.  It owns only the
     * compute pipelines and small per-dispatch parameter buffers; all working
     * data buffers are caller-provided.
     *
     * The algorithm knows nothing about what a key encodes: it never decodes,
     * reconstructs or re-encodes a record.  A caller whose record is not a scalar
     * packs the record identity into the key itself and restores it after
     * compaction (the broad phase packs `a * shape_count + b` and unpacks in its
     * own pass).
     *
     * Algorithm:
     *   1. Flag unique:  flags[i] = (i==0 || keys[i] != keys[i-1]) ? 1 : 0
     *                     Writes to original_flags buffer.
     *   2. Copy flags -> offsets buffer (separate buffer for scan output).
     *   3. Exclusive prefix sum on offsets (via external ParallelScan, in-place).
     *   4. Scatter:      if original_flags[i] == 1, write keys[i] to keys[offsets[i]].
     *   5. Write total unique count to count buffer.
     *
     * The output overwrites the input key buffer with the compact unique keys.
     *
     * Caller-provided resources:
     *   - Key buffer (sorted input, compact output)
     *   - Original flags buffer (max_elem_count uints)
     *   - Flag offsets buffer (max_elem_count uints, same size as flags)
     *   - Count buffer (1 uint, host-visible)
     *   - Element count buffer (GPU-side uint, actual count at execution time)
     *   - ParallelScan instance + its scratch buffer (for prefix sum on offsets)
     */
    class PHYSICS_API CompactUnique {
    public:
        explicit CompactUnique(Rhi::DeviceContext &device_context, uint32_t max_elem_count);

        ~CompactUnique();

        CompactUnique(const CompactUnique &) = delete;
        CompactUnique &operator=(const CompactUnique &) = delete;
        CompactUnique(CompactUnique &&) = delete;
        CompactUnique &operator=(CompactUnique &&) = delete;

        static constexpr size_t GetRequiredFlagBytes(uint32_t max_elem_count) noexcept {
            return static_cast<size_t>(max_elem_count) * sizeof(uint32_t);
        }

        static constexpr size_t GetRequiredScratchBytes() noexcept {
            return sizeof(uint32_t);
        }

        /**
         * @brief Record compact-unique dispatches to the command buffer.
         *
         * @param cb               Command buffer in recording state.
         * @param keys_buf         Key buffer (sorted input, compact output).
         * @param flags_buf        Original flags buffer (receives 0/1 per element).
         * @param offsets_buf      Offsets buffer (receives exclusive prefix sum of flags).
         * @param count_buf        Unique count buffer (1 uint, host-visible).
         * @param scan_scratch_buf ParallelScan block-sums scratch buffer.
         * @param scan             External ParallelScan instance.
         * @param elem_count_buf   Element count buffer (1 uint, written by upstream passes).
         * @param elem_capacity    Buffer capacity in keys (for dispatch sizing).
         *
         * @pre elem_capacity <= max_elem_count
         */
        void Record(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &keys_buf,
            Rhi::ComputeBuffer &flags_buf,
            Rhi::ComputeBuffer &offsets_buf,
            Rhi::ComputeBuffer &count_buf,
            Rhi::ComputeBuffer &scan_scratch_buf,
            ParallelScan &scan,
            Rhi::ComputeBuffer &elem_count_buf,
            uint32_t elem_capacity
        );

        bool IsInitialized() const noexcept;

        uint32_t GetMaxElemCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_GPU_ALGORITHM_COMPACTUNIQUE_INCLUDED
