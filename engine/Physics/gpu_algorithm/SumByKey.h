#ifndef ENGINE_PHYSICS_GPU_ALGORITHM_SUMBYKEY_INCLUDED
#define ENGINE_PHYSICS_GPU_ALGORITHM_SUMBYKEY_INCLUDED

#include "../physics_export.h"

#include <cstdint>
#include <memory>
#include <variant>
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
     *   - Level 0 reads the call's `capacity` keys from the caller's sorted key
     *     array and gathers each element's values from the packed value buffer at
     *     the payload index its payload array carries (`value(c) =
     *     values[c * capacity + payload[e]]`).  The caller therefore needs no
     *     separate key array: the payload its sort carried along *is* the value
     *     index.  An entry whose payload index is outside `[0, capacity)`
     *     contributes zero and is never read from the value buffer; its key is
     *     left unchanged, since rewriting it would break the ascending-order
     *     requirement and could split a real key's run.
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
     * elements carry real input data; it bounds **data extent only** and its
     * source is the call's `EntryCountSource`: a CPU-known value travels in the
     * push-constant block, a GPU-produced one is read from the caller's buffer
     * at execution time.  An entry count equal to `capacity` behaves exactly as
     * if the bound were absent.
     *
     * The record regions for the intermediate levels live in the instance's own
     * storage, partitioned by level with offsets derived per call from the call's
     * capacity, its channel count and the device's descriptor offset alignment.
     * The caller allocates nothing and cannot get the layout wrong.
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
     * never needs rebuilding when the caller's geometry changes.  The record
     * storage is shared by every call, so two `Record` calls on one instance in
     * the same command buffer have an execution-order dependency through it and
     * the caller must record a barrier between them.
     *
     * Owned GPU resources: the compute pipelines, loaded lazily on the first
     * `Record` (matching `RadixSort`), and the record-region storage, grown
     * geometrically and reused across calls.
     */
    class PHYSICS_API SumByKey {
    public:
        static constexpr uint32_t kBlockSize = 256u;
        static constexpr uint32_t kMaxChannels = 8u;

        /**
         * @brief Where a call's entry count comes from.
         *
         * A `uint32_t` is a CPU-known count: it travels in the push-constant
         * block and the call binds no count buffer. A `const Rhi::ComputeBuffer *`
         * is a GPU-produced count: the shader reads it from that buffer at
         * execution time. The source selects the kernel the call records.
         */
        using EntryCountSource = std::variant<uint32_t, const Rhi::ComputeBuffer *>;

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
         * @brief Record the full recursive reduction to the command buffer.
         *
         * Reduces the sorted key array @p keys_in_buf — gathering each entry's
         * values from @p values_in_buf at the slot its payload index names — and
         * writes per-key sums to @p out_values_buf (channel-major, stride
         * @p max_key_value).  Exactly `GetNumLevels(capacity)` compute dispatches
         * are recorded, with a full compute barrier between consecutive levels;
         * the caller is responsible for the outer barriers around the whole
         * `Record`.
         *
         * Level 0 always dispatches `ceil(capacity / 256)` workgroups and each
         * level reads its input as an array of `capacity` / `R_i` elements, so
         * the record chain is rewritten in full on every call.  The entry count
         * bounds only which **values** are read.
         *
         * All level parameters (region offsets, element counts, workgroup
         * counts, channel stride, channel count, key bound, gather mode) are
         * passed as push constants.  Bindings:
         *   - `KeysIn`    — the caller's sorted key array (`capacity` uints) at
         *     level 0; the record region's keys at level >= 1.
         *   - `PayloadIn` — the caller's payload-index array (`capacity` uints),
         *     read at level 0 only, bound at every level.
         *   - `ValuesIn`  — caller's packed values at level 0 (channel-major,
         *     stride `capacity`); the record region's values at level >= 1.
         *   - `RecKeys` / `RecValues` — the instance's record storage (level >= 1
         *     input, non-final-level output). Their offsets are multiples of the
         *     device's storage-buffer offset alignment.
         *   - `OutValues` — output (channel-major, stride @p max_key_value).
         *
         * @param cb               Command buffer in recording state.
         * @param keys_in_buf      Sorted key array (`capacity` uints).
         * @param payload_in_buf   Payload-index array (`capacity` uints); entry
         *                         `e`'s values are gathered from
         *                         `payload_in_buf[e]`.
         * @param values_in_buf    Packed value buffer (channel-major, indexed by
         *                         the payload index).
         * @param out_values_buf   Output value buffer (channel-major, stride
         *                         @p max_key_value), `num_channels *
         *                         max_key_value` floats.
         * @param entry_count      Entry-count source: a CPU-known value, or a
         *                         bound buffer (1 uint) holding a GPU-produced
         *                         one. A bound buffer is read at level 0 only.
         * @param capacity         Level-0 element count (the entry capacity);
         *                         must be greater than zero.
         * @param num_channels     Number of float value channels per record,
         *                         in `[1, kMaxChannels]`.
         * @param max_key_value    Number of output key slots (keys in
         *                         `[0, max_key_value)` are writable); must be
         *                         greater than zero.  Keys at or above it — the
         *                         caller's invalid-slot sentinel among them — are
         *                         never written to the output.
         *
         * @throws std::invalid_argument if `num_channels` is outside
         *         `[1, kMaxChannels]`, or `capacity` / `max_key_value` is zero.
         * @throws std::runtime_error if a bound buffer is smaller than the size
         *         the call's geometry implies.
         */
        void Record(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &keys_in_buf,
            Rhi::ComputeBuffer &payload_in_buf,
            Rhi::ComputeBuffer &values_in_buf,
            Rhi::ComputeBuffer &out_values_buf,
            EntryCountSource entry_count,
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
