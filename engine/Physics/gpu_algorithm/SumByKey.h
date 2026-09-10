#ifndef ENGINE_PHYSICS_GPU_ALGORITHM_SUMBYKEY_INCLUDED
#define ENGINE_PHYSICS_GPU_ALGORITHM_SUMBYKEY_INCLUDED

#include "../physics_export.h"

#include <cstdint>
#include <memory>
#include <vector>

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
     * @brief GPU recursive segmented reduction over sorted (key, value) arrays.
     *
     * SumByKey reduces a caller-provided, ascending-sorted array of `(key,
     * value[0..N))` records into per-key sums: `out[key][c] = sum of
     * values[c] over all records whose key == key`.  The input is sorted by
     * key so every value with a given key forms one contiguous run (segment);
     * within each run the relative order is irrelevant (reduction is
     * unordered, floating-point rounding may vary).
     *
     * It is a *recursive block-level segmented reduction*:
     *   - Level 0 reads `max_entries` records from the caller's sorted keys and
     *     packed value buffers.
     *   - Each workgroup reduces one 256-record block in shared memory (with
     *     integer-free same-key doubling and DEAD marking), writing the sums of
     *     segments fully contained in the block directly to the output and
     *     emitting at most two boundary records per block into the next level.
     *   - Levels 1..k-2 read the previous level's records and write their own
     *     boundary records; the final single-block level writes every remaining
     *     segment directly.
     *
     * Record regions for the intermediate levels live in one caller-provided
     * buffer partitioned by level (offsets fixed at construction), mirroring
     * the `ParallelScan::GetRequiredBlockSumsBytes` pattern.
     *
     * The `num_channels` float values per record are batched into a single
     * buffer in channel-major layout: `value(c, i) = buf[c * stride + i]`,
     * where `stride` is the current level's element count.  `num_channels` and
     * the level geometry travel as push constants (never specialization
     * constants), so one shader recurses at every level and all channels
     * reduce in a single pass per level.
     *
     * Owned GPU resources: none (all working buffers caller-provided).  Owns
     * only the compute pipeline, whose shader is loaded lazily on first
     * `Record` (matching `RadixSort`).
     */
    class PHYSICS_API SumByKey {
    public:
        static constexpr uint32_t kBlockSize = 256u;
        static constexpr uint32_t kMaxChannels = 8u;
        /// Keys greater than or equal to `max_key_value` (incl. the INVALID
        /// sentinel) are ignored and never write to the output.
        static constexpr uint32_t kInvalidKey = 0xFFFFFu;

        /**
         * @brief Construct a SumByKey reducer.
         *
         * Allocates no GPU resources; the compute stage and shader are created
         * lazily on the first `Record` call.
         *
         * @param device_context  Device context for pipeline creation.
         * @param max_entries     Maximum number of records ever reduced
         *                        (defines level-0 geometry and record sizing).
         * @param max_key_value   Number of output key slots (keys in
         *                        `[0, max_key_value)` are writable).
         * @param num_channels    Number of float value channels per record,
         *                        in `[1, kMaxChannels]`.
         *
         * @throws std::invalid_argument if `num_channels` is outside `[1, 8]`
         *         or `max_entries` / `max_key_value` is zero.
         */
        SumByKey(
            Rhi::DeviceContext &device_context, uint32_t max_entries, uint32_t max_key_value, uint32_t num_channels
        );

        ~SumByKey();

        SumByKey(const SumByKey &) = delete;
        SumByKey &operator=(const SumByKey &) = delete;
        SumByKey(SumByKey &&) = delete;
        SumByKey &operator=(SumByKey &&) = delete;

        /**
         * @brief Number of recursion levels `k` for a given element count.
         *
         * `R_0 = max_entries`, `R_{i+1} = 2 * ceil(R_i / 256)` until
         * `R_k <= 256`.  Always at least 1; at most 4 for
         * `max_entries <= 2^28`.
         *
         * @param max_entries  Element count whose level geometry is requested.
         * @return Number of levels (== number of `Record` dispatches).
         */
        static uint32_t GetNumLevels(uint32_t max_entries) noexcept;

        /**
         * @brief Required record-buffer size in bytes.
         *
         * The record regions for levels 1..k-1: `(R_1 + ... + R_{k-1}) *
         * (4 + 4 * num_channels)` bytes (one key uint plus `num_channels`
         * floats per record).  Zero when `k == 1`.
         *
         * @param max_entries   Element count whose geometry is requested.
         * @param num_channels  Number of value channels per record.
         * @return Minimum record-buffer size in bytes.
         */
        static size_t GetRequiredRecordsBytes(uint32_t max_entries, uint32_t num_channels) noexcept;

        /**
         * @brief Record the full recursive reduction to the command buffer.
         *
         * Reduces the sorted keys in @p keys_in_buf with the packed values in
         * @p values_in_buf and writes per-key sums to @p out_values_buf
         * (channel-major, stride `max_key_value`).  Exactly `GetNumLevels`
         * compute dispatches are recorded, with a full compute barrier between
         * consecutive levels.  The caller is responsible for the outer barriers
         * around the whole `Record`.
         *
         * All level parameters (region offsets, element counts, workgroup
         * counts, channel stride, channel count, key bound) are passed as push
         * constants.  Bindings:
         *   - `KeysIn`    — caller's sorted keys (`max_entries` uints).
         *   - `ValuesIn`  — caller's packed values (channel-major).
         *   - `RecKeys` / `RecValues` — the record buffer (level >= 1 input,
         *     non-final-level output).
         *   - `OutValues` — output (channel-major, stride `max_key_value`).
         *
         * @param cb               Command buffer in recording state.
         * @param keys_in_buf      Sorted key array (`max_entries` uints).
         * @param values_in_buf    Packed value buffer (channel-major).
         * @param records_buf      Record buffer, >= GetRequiredRecordsBytes.
         * @param out_values_buf   Output value buffer (channel-major, stride
         *                         `max_key_value`), `num_channels * max_key_value`
         *                         floats.
         */
        void Record(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &keys_in_buf,
            Rhi::ComputeBuffer &values_in_buf,
            Rhi::ComputeBuffer &records_buf,
            Rhi::ComputeBuffer &out_values_buf
        );

        bool IsInitialized() const noexcept;

        /// Get the configured maximum record count.
        uint32_t GetMaxEntries() const noexcept;
        /// Get the configured maximum key value (output slot count).
        uint32_t GetMaxKeyValue() const noexcept;
        /// Get the configured channel count.
        uint32_t GetNumChannels() const noexcept;
        /// Number of recursion levels for this instance's `max_entries`.
        uint32_t GetNumLevels() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_GPU_ALGORITHM_SUMBYKEY_INCLUDED
