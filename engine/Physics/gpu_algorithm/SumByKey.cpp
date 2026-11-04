#include "SumByKey.h"

#include "Physics/PhysicsDispatch.h"
#include "Physics/PhysicsSpirvLoader.h"
#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Pipeline/ComputeKernel.h"

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace Engine {

    // Push-constant layout, matching the SumByKeyPush block in
    // engine/Physics/shader/algorithm/sum_by_key.comp (std430).  The buffer-count
    // form reads its entry count from a binding, so it carries no count field.
    struct SumByKeyPush {
        uint32_t input_count;
        uint32_t num_channels;
        uint32_t max_key_value;
        uint32_t record_stride;
        uint32_t gather_payload;
    };
    static_assert(sizeof(SumByKeyPush) == 20, "SumByKeyPush must be 20 bytes");

    // Push-constant layout of the value-count form (sum_by_key_push.comp): the
    // same block plus the CPU-known entry count.
    struct SumByKeyValuePush {
        uint32_t input_count;
        uint32_t num_channels;
        uint32_t max_key_value;
        uint32_t record_stride;
        uint32_t gather_payload;
        uint32_t entry_count;
    };
    static_assert(sizeof(SumByKeyValuePush) == 24, "SumByKeyValuePush must be 24 bytes");

    // Level geometry helper shared by the static sizing functions and Record.
    struct LevelGeometry {
        // R[0] = capacity; R[i+1] = 2*ceil(R[i]/256) until R.back() <= 256.
        std::vector<uint32_t> r;
    };

    LevelGeometry ComputeGeometry(uint32_t capacity) {
        LevelGeometry g;
        g.r.push_back(capacity);
        while (g.r.back() > SumByKey::kBlockSize) {
            uint32_t nb = (g.r.back() + SumByKey::kBlockSize - 1u) / SumByKey::kBlockSize;
            g.r.push_back(2u * nb);
        }
        return g;
    }

    // One stored record region (levels 1..k-1): how many records it holds. Each region owns its own
    // key and value buffers - see `Impl::record_regions` - so there is no offset to compute and no
    // alignment padding between regions.
    struct RecordRegion {
        uint32_t count = 0u; // R_i records
    };

    struct RecordLayout {
        std::vector<RecordRegion> regions;
    };

    RecordLayout ComputeRecordLayout(const LevelGeometry &geometry) {
        RecordLayout layout;
        if (geometry.r.size() <= 1u) return layout;
        layout.regions.reserve(geometry.r.size() - 1u);
        for (size_t i = 1u; i < geometry.r.size(); ++i) {
            layout.regions.push_back(RecordRegion{geometry.r[i]});
        }
        return layout;
    }

    struct SumByKey::Impl {
        Rhi::DeviceContext &device_context;
        bool initialized = false;

        // One kernel per count source: the buffer-count form binds the caller's
        // entry-count buffer, the value-count form takes the count in its push
        // block and binds nothing extra.
        Rhi::ComputeKernel *reduce_kernel = nullptr;
        Rhi::ComputeKernel *reduce_push_kernel = nullptr;

        /// Owned record-region storage: one key buffer and one value buffer per level.
        ///
        /// A level reads the previous level's region and writes its own, so both would be bound
        /// as two writable storage bindings of one buffer if the regions shared storage - which
        /// WebGPU forbids. Giving each level its own pair makes every level's input and output
        /// distinct buffers, and the regions are tiny (two slots per block).
        struct RegionBuffers {
            std::unique_ptr<Rhi::ComputeBuffer> keys{};
            std::unique_ptr<Rhi::ComputeBuffer> values{};
        };
        std::vector<RegionBuffers> record_regions{};
        /// Bound for the bindings a level does not write (the final level, single-level calls).
        std::unique_ptr<Rhi::ComputeBuffer> record_sink_keys{};
        std::unique_ptr<Rhi::ComputeBuffer> record_sink_values{};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            reduce_kernel = &LoadPhysicsKernel(device_context, "algorithm/sum_by_key.comp.spv", "SumByKey");
            reduce_push_kernel =
                &LoadPhysicsKernel(device_context, "algorithm/sum_by_key_push.comp.spv", "SumByKey Push");
        }

        /// @brief Grow each level's record-region storage to this call's geometry.
        void EnsureRecordRegions(const std::vector<RecordRegion> &regions, uint32_t num_channels) {
            if (record_regions.size() < regions.size()) {
                record_regions.resize(regions.size());
            }
            const Rhi::AllocatorState &allocator = device_context.GetAllocatorState();
            for (size_t index = 0u; index < regions.size(); ++index) {
                const size_t key_bytes =
                    std::max<size_t>(static_cast<size_t>(regions[index].count) * sizeof(uint32_t), sizeof(uint32_t));
                const size_t value_bytes = std::max<size_t>(
                    static_cast<size_t>(num_channels) * regions[index].count * sizeof(uint32_t), sizeof(uint32_t)
                );
                const std::string suffix = std::to_string(index);
                Rhi::EnsureComputeBuffer(
                    record_regions[index].keys, allocator, key_bytes, false, "SumByKey RecKeys " + suffix
                );
                Rhi::EnsureComputeBuffer(
                    record_regions[index].values, allocator, value_bytes, false, "SumByKey RecValues " + suffix
                );
            }
            Rhi::EnsureComputeBuffer(record_sink_keys, allocator, sizeof(uint32_t), false, "SumByKey RecKeys Sink");
            Rhi::EnsureComputeBuffer(record_sink_values, allocator, sizeof(uint32_t), false, "SumByKey RecValues Sink");
        }
    };

    SumByKey::SumByKey(Rhi::DeviceContext &device_context) : m_impl(std::make_unique<Impl>(device_context)) {
    }

    SumByKey::~SumByKey() = default;

    uint32_t SumByKey::GetNumLevels(uint32_t capacity) noexcept {
        if (capacity == 0u) return 0u;
        return static_cast<uint32_t>(ComputeGeometry(capacity).r.size());
    }

    bool SumByKey::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    void SumByKey::Record(
        Rhi::CommandEncoder &encoder,
        Rhi::ComputeBuffer &keys_in_buf,
        Rhi::ComputeBuffer &payload_in_buf,
        Rhi::ComputeBuffer &values_in_buf,
        Rhi::ComputeBuffer &out_values_buf,
        EntryCountSource entry_count,
        uint32_t capacity,
        uint32_t num_channels,
        uint32_t max_key_value
    ) {
        // Geometry is no longer a construction parameter, so the argument checks
        // the constructor used to perform happen here, on the call's values.
        if (num_channels == 0u || num_channels > kMaxChannels) {
            throw std::invalid_argument("SumByKey: num_channels must be in [1, kMaxChannels]");
        }
        if (capacity == 0u) {
            throw std::invalid_argument("SumByKey: capacity must be > 0");
        }
        if (max_key_value == 0u) {
            throw std::invalid_argument("SumByKey: max_key_value must be > 0");
        }

        // The count source selects the kernel. A CPU-known count travels in the
        // push block and binds nothing; a GPU-produced count is read from the
        // caller's buffer, whose size is validated here.
        const auto *entry_count_buf = std::get_if<const Rhi::ComputeBuffer *>(&entry_count);
        const auto *entry_count_value = std::get_if<uint32_t>(&entry_count);
        if (entry_count_buf != nullptr && *entry_count_buf == nullptr) {
            throw std::invalid_argument("SumByKey: entry-count source buffer must not be null");
        }
        if (entry_count_buf != nullptr && (*entry_count_buf)->GetSize() < sizeof(uint32_t)) {
            throw std::runtime_error("SumByKey::Record: entry-count buffer must hold at least one uint");
        }

        // The whole level chain is a pure function of this call's capacity.
        const LevelGeometry geometry = ComputeGeometry(capacity);
        const uint32_t k = static_cast<uint32_t>(geometry.r.size());
        if (k == 0u) return;

        const RecordLayout layout = ComputeRecordLayout(geometry);

        // The construction-time bound is gone, so the out-of-bounds guard is
        // checked per call against the buffers actually bound for this call.
        const size_t keys_bytes = static_cast<size_t>(capacity) * sizeof(uint32_t);
        const size_t values_bytes = static_cast<size_t>(num_channels) * capacity * sizeof(uint32_t);
        const size_t out_bytes = static_cast<size_t>(num_channels) * max_key_value * sizeof(uint32_t);
        if (keys_in_buf.GetSize() < keys_bytes) {
            throw std::runtime_error("SumByKey::Record: key buffer is smaller than capacity * sizeof(uint32_t)");
        }
        if (payload_in_buf.GetSize() < keys_bytes) {
            throw std::runtime_error("SumByKey::Record: payload buffer is smaller than capacity * sizeof(uint32_t)");
        }
        if (values_in_buf.GetSize() < values_bytes) {
            throw std::runtime_error("SumByKey::Record: value buffer is smaller than num_channels * capacity floats");
        }
        if (out_values_buf.GetSize() < out_bytes) {
            throw std::runtime_error(
                "SumByKey::Record: output buffer is smaller than num_channels * max_key_value floats"
            );
        }
        if (entry_count_buf == nullptr && entry_count_value == nullptr) {
            throw std::invalid_argument("SumByKey: entry-count source is empty");
        }

        m_impl->EnsureInitialized();
        // Every level owns its record region, so the bindings below never alias; the sinks keep
        // the descriptor set complete for a level that writes no records.
        const std::vector<RecordRegion> &regions = layout.regions;
        m_impl->EnsureRecordRegions(regions, num_channels);

        // Bindings that do not change with the level: the caller's level-0
        // key array, the caller's payload-index array (read only where
        // `gather_payload` is set — an untouched descriptor is never dereferenced),
        // and the output.
        const Rhi::ComputeKernelResource keys_in_level0 =
            Rhi::ComputeKernelResource::Buffer(keys_in_buf, 0u, keys_bytes);
        const Rhi::ComputeKernelResource payload_in =
            Rhi::ComputeKernelResource::Buffer(payload_in_buf, 0u, keys_bytes);
        const Rhi::ComputeKernelResource out_values = Rhi::ComputeKernelResource::Buffer(out_values_buf, 0u, out_bytes);

        for (uint32_t level = 0u; level < k; ++level) {
            const uint32_t input_count = geometry.r[level];
            const uint32_t num_blocks = (input_count + kBlockSize - 1u) / kBlockSize;

            // ---- Read source for this level ----
            Rhi::ComputeKernelResource keys_in = keys_in_level0;
            Rhi::ComputeKernelResource values_in = Rhi::ComputeKernelResource::Buffer(values_in_buf, 0u, values_bytes);
            if (level > 0u) {
                const auto &reg = regions[level - 1u];
                keys_in = Rhi::ComputeKernelResource::Buffer(
                    *m_impl->record_regions[level - 1u].keys, 0u, static_cast<size_t>(reg.count) * sizeof(uint32_t)
                );
                values_in = Rhi::ComputeKernelResource::Buffer(
                    *m_impl->record_regions[level - 1u].values,
                    0u,
                    static_cast<size_t>(num_channels) * reg.count * sizeof(uint32_t)
                );
            }

            // ---- Write target for this level ----
            uint32_t record_stride = 0u;
            Rhi::ComputeKernelResource rec_keys{};
            Rhi::ComputeKernelResource rec_values{};
            if (level + 1u < k) {
                // Non-final: emit records into the region of R_{level+1}, which is this level's own.
                const auto &out_reg = regions[level]; // index level == region R_{level+1}
                rec_keys = Rhi::ComputeKernelResource::Buffer(
                    *m_impl->record_regions[level].keys, 0u, static_cast<size_t>(out_reg.count) * sizeof(uint32_t)
                );
                rec_values = Rhi::ComputeKernelResource::Buffer(
                    *m_impl->record_regions[level].values,
                    0u,
                    static_cast<size_t>(num_channels) * out_reg.count * sizeof(uint32_t)
                );
                record_stride = out_reg.count;
            } else {
                // Final level (or a single-level call): no record output, so the bindings point at
                // dedicated sinks. They must not be the input, the output or each other: all of
                // those are writable bindings of this dispatch.
                rec_keys = Rhi::ComputeKernelResource::Buffer(*m_impl->record_sink_keys, 0u, sizeof(uint32_t));
                rec_values = Rhi::ComputeKernelResource::Buffer(*m_impl->record_sink_values, 0u, sizeof(uint32_t));
            }

            const uint32_t gather_payload = (level == 0u) ? 1u : 0u;
            if (entry_count_buf != nullptr) {
                const SumByKeyPush params{input_count, num_channels, max_key_value, record_stride, gather_payload};
                m_impl->reduce_kernel->Dispatch(
                    encoder,
                    {{"KeysIn", keys_in},
                     {"ValuesIn", values_in},
                     {"PayloadIn", payload_in},
                     {"RecKeys", rec_keys},
                     {"RecValues", rec_values},
                     {"OutValues", out_values},
                     {"EntryCount", Rhi::ComputeKernelResource::Buffer(**entry_count_buf, 0u, sizeof(uint32_t))}},
                    num_blocks,
                    1,
                    1,
                    params
                );
            } else {
                const SumByKeyValuePush params{
                    input_count, num_channels, max_key_value, record_stride, gather_payload, *entry_count_value
                };
                m_impl->reduce_push_kernel->Dispatch(
                    encoder,
                    {{"KeysIn", keys_in},
                     {"ValuesIn", values_in},
                     {"PayloadIn", payload_in},
                     {"RecKeys", rec_keys},
                     {"RecValues", rec_values},
                     {"OutValues", out_values}},
                    num_blocks,
                    1,
                    1,
                    params
                );
            }

            // The next level reads what this one just wrote.
            if (level + 1u < k) {
                DispatchBarrier(encoder);
            }
        }
    }
} // namespace Engine
