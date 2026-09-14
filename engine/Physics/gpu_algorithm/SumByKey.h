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
     *   - Level 0 reads the call's `capacity` `(key, slot)` pairs from the
     *     caller's sorted pair array and gathers each element's values from the
     *     packed value buffer at the slot that pair carries (`value(c) =
     *     values[c * capacity + slot]`).  The caller therefore needs neither a
     *     separate key array nor a permutation map: the payload its sort
     *     carried along *is* the value index.  An entry whose slot is outside
     *     `[0, capacity)` contributes zero and is never read from the value
     *     buffer; its key is left unchanged, since rewriting it would break the
     *     ascending-order requirement and could split a real key's run.
     *   - Each workgroup reduces one 256-record block in shared memory (with
     *     integer-free same-key doubling and DEAD marking), writing the sums of
     *     segments fully contained in the block directly to the output and
     *     emitting at most two boundary records per block into the next level.
     *   - Levels 1..k-2 read the previous level's records and write their own
     *     boundary records; the final single-block level writes every remaining
     *     segment directly.
     *
     * **Two lengths bound level 0 and are not interchangeable.**  The *level
     * element count* is the length of the array a level reads (`capacity` at
     * level 0, `R_i` above); it alone defines the element-index bound, the run
     * classification's per-block real extent, the record-region partition and
     * the per-level dispatch count, and it is a pure function of `capacity`, so
     * `Record` computes the whole chain.  The *entry count* is how many leading
     * elements carry real input data; it bounds **data extent only** and is read
     * by the shader from the caller's entry-count buffer at execution time,
     * because the producing pass runs on the GPU.  An entry count equal to
     * `capacity` behaves exactly as if the bound were absent.
     *
     * Record regions for the intermediate levels live in one caller-provided
     * buffer partitioned by level (offsets derived per call), mirroring the
     * `ParallelScan::GetRequiredBlockSumsBytes` pattern.
     *
     * The `num_channels` float values per record are batched into a single
     * buffer in channel-major layout: `value(c, i) = buf[c * stride + i]`,
     * where `stride` is the current level's element count.  `num_channels` and
     * the level geometry travel as push constants (never specialization
     * constants), so one shader recurses at every level and all channels
     * reduce in a single pass per level.
     *
     * The instance holds no geometry: every `Record` call supplies its own, so
     * one instance serves any number of geometries within a single frame and
     * never needs rebuilding when the caller's geometry changes.
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
         * Allocates no GPU resources and stores no geometry; the compute stage
         * and shader are created lazily on the first `Record` call.
         *
         * @param device_context  Device context for pipeline creation.
         */
        explicit SumByKey(Rhi::DeviceContext &device_context);

        ~SumByKey();

        SumByKey(const SumByKey &) = delete;
        SumByKey &operator=(const SumByKey &) = delete;
        SumByKey(SumByKey &&) = delete;
        SumByKey &operator=(SumByKey &&) = delete;

        /**
         * @brief Number of recursion levels `k` for a given element count.
         *
         * `R_0 = capacity`, `R_{i+1} = 2 * ceil(R_i / 256)` until
         * `R_k <= 256`.  Always at least 1; at most 4 for `capacity <= 2^28`.
         *
         * @param capacity  Element count whose level geometry is requested.
         * @return Number of levels (== number of `Record` dispatches).
         */
        static uint32_t GetNumLevels(uint32_t capacity) noexcept;

        /**
         * @brief Required record-buffer size in bytes.
         *
         * The record regions for levels 1..k-1: `(R_1 + ... + R_{k-1}) *
         * (4 + 4 * num_channels)` bytes (one key uint plus `num_channels`
         * floats per record).  Zero when `k == 1`.
         *
         * @param capacity      Element count whose geometry is requested.
         * @param num_channels  Number of value channels per record.
         * @return Minimum record-buffer size in bytes.
         */
        static size_t GetRequiredRecordsBytes(uint32_t capacity, uint32_t num_channels) noexcept;

        /**
         * @brief Record the full recursive reduction to the command buffer.
         *
         * Reduces the sorted `(key, slot)` pairs in @p pairs_in_buf, gathering
         * their values from @p values_in_buf, and writes per-key sums to
         * @p out_values_buf (channel-major, stride @p max_key_value).  Exactly
         * `GetNumLevels(capacity)` compute dispatches are recorded, with a full
         * compute barrier between consecutive levels; the caller is responsible
         * for the outer barriers around the whole `Record`.
         *
         * Level 0 always dispatches `ceil(capacity / 256)` workgroups and each
         * level reads its input as an array of `capacity` / `R_i` elements, so
         * the record chain is rewritten in full on every call.  The entry count
         * read from @p entry_count_buf bounds only which **values** are read.
         *
         * All level parameters (region offsets, element counts, workgroup
         * counts, channel stride, channel count, key bound, gather mode) are
         * passed as push constants.  Bindings:
         *   - `PairsIn`   — caller's sorted `(key, slot)` pairs
         *     (`capacity` uvec2); read at level 0 only, bound at every level.
         *   - `KeysIn`    — record keys (`capacity` uints at level 0, where it
         *     is not read; the region's keys at level >= 1).
         *   - `ValuesIn`  — caller's packed values at level 0 (channel-major,
         *     stride `capacity`); the record region's values at level >= 1.
         *   - `RecKeys` / `RecValues` — the record buffer (level >= 1 input,
         *     non-final-level output).
         *   - `OutValues` — output (channel-major, stride @p max_key_value).
         *   - `EntryCount` — the call's entry count (1 uint), bound at every
         *     level and read only at level 0.
         *
         * @param cb               Command buffer in recording state.
         * @param pairs_in_buf     Sorted `(key, slot)` pair array
         *                         (`capacity` uvec2, i.e. `2 * capacity` uints).
         * @param values_in_buf    Packed value buffer (channel-major, indexed by
         *                         the pair's slot).
         * @param records_buf      Record buffer, >= GetRequiredRecordsBytes.
         * @param out_values_buf   Output value buffer (channel-major, stride
         *                         @p max_key_value), `num_channels *
         *                         max_key_value` floats.
         * @param entry_count_buf  Entry count buffer (1 uint, read on the GPU).
         * @param capacity         Level-0 element count (the entry capacity);
         *                         must be greater than zero.
         * @param num_channels     Number of float value channels per record,
         *                         in `[1, kMaxChannels]`.
         * @param max_key_value    Number of output key slots (keys in
         *                         `[0, max_key_value)` are writable); must be
         *                         greater than zero.
         *
         * @throws std::invalid_argument if `num_channels` is outside
         *         `[1, kMaxChannels]`, or `capacity` / `max_key_value` is zero.
         * @throws std::runtime_error if a bound buffer is smaller than the size
         *         the call's geometry implies.
         */
        void Record(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &pairs_in_buf,
            Rhi::ComputeBuffer &values_in_buf,
            Rhi::ComputeBuffer &records_buf,
            Rhi::ComputeBuffer &out_values_buf,
            Rhi::ComputeBuffer &entry_count_buf,
            uint32_t capacity,
            uint32_t num_channels,
            uint32_t max_key_value
        );

        bool IsInitialized() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Engine

#endif // ENGINE_PHYSICS_GPU_ALGORITHM_SUMBYKEY_INCLUDED
