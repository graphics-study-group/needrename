#ifndef ENGINE_PHYSICS_GPU_ALGORITHM_COMPACTUNIQUE_INCLUDED
#define ENGINE_PHYSICS_GPU_ALGORITHM_COMPACTUNIQUE_INCLUDED

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
     * @brief GPU compact-unique post-processing pass.
     *
     * Given a sorted uvec2 array, CompactUnique removes adjacent duplicates
     * and produces a contiguous output of unique entries.  It owns only the
     * compute pipelines and small per-dispatch parameter buffers; all working
     * data buffers are caller-provided.
     *
     * Algorithm:
     *   1. Flag unique:  flags[i] = (i==0 || pairs[i] != pairs[i-1]) ? 1 : 0
     *                     Writes to original_flags buffer.
     *   2. Copy flags -> offsets buffer (separate buffer for scan output).
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
         * @param pairs_buf        Pairs buffer (sorted input, compact output).
         * @param flags_buf        Original flags buffer (receives 0/1 per element).
         * @param offsets_buf      Offsets buffer (receives exclusive prefix sum of flags).
         * @param count_buf        Unique count buffer (1 uint, host-visible).
         * @param scan_scratch_buf ParallelScan block-sums scratch buffer.
         * @param scan             External ParallelScan instance.
         * @param pair_count_buf   Pair count buffer (1 uint, written by upstream passes).
         * @param elem_capacity    Buffer capacity in pairs (for dispatch sizing).
         *
         * @pre elem_capacity <= max_elem_count
         */
        void Record(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &pairs_buf,
            Rhi::ComputeBuffer &flags_buf,
            Rhi::ComputeBuffer &offsets_buf,
            Rhi::ComputeBuffer &count_buf,
            Rhi::ComputeBuffer &scan_scratch_buf,
            ParallelScan &scan,
            Rhi::ComputeBuffer &pair_count_buf,
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
