#include "SumByKey.h"

#include <vulkan/vulkan.hpp>

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

    // One stored record region (levels 1..k-1): byte offsets into the caller's
    // record buffer for its keys and its values, plus its record count.  Derived
    // per call, because the instance holds no geometry.
    struct RecordRegion {
        uint32_t count = 0u; // R_i records
        size_t key_offset = 0u;
        size_t val_offset = 0u;
    };

    // The padded record-region partition: one region per level 1..k-1, each key
    // array and value array starting on the device's descriptor offset
    // alignment, and the total size the caller's record buffer must provide.
    struct RecordLayout {
        std::vector<RecordRegion> regions;
        size_t total_bytes = 0u;
    };

    size_t AlignUp(size_t value, size_t alignment) noexcept {
        if (alignment <= 1u) return value;
        return ((value + alignment - 1u) / alignment) * alignment;
    }

    // Padding sits between a region's key array and its value array and between
    // consecutive regions, never inside a value array: a region's channel stride
    // stays exactly its own record count, which is what the shader's
    // `record_stride` field carries.
    RecordLayout ComputeRecordLayout(const LevelGeometry &geometry, uint32_t num_channels, size_t alignment) {
        RecordLayout layout;
        if (geometry.r.size() <= 1u) return layout;
        layout.regions.reserve(geometry.r.size() - 1u);

        size_t cursor = 0u;
        for (size_t i = 1u; i < geometry.r.size(); ++i) {
            RecordRegion reg;
            reg.count = geometry.r[i];
            reg.key_offset = AlignUp(cursor, alignment);
            reg.val_offset = AlignUp(reg.key_offset + static_cast<size_t>(reg.count) * sizeof(uint32_t), alignment);
            layout.regions.push_back(reg);
            cursor = reg.val_offset + static_cast<size_t>(num_channels) * reg.count * sizeof(uint32_t);
        }
        layout.total_bytes = cursor;
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

        /// Owned record-region storage, grown geometrically and reused across calls.
        std::unique_ptr<Rhi::ComputeBuffer> records{};

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

        /// @brief Grow the record-region storage to @p bytes.
        void EnsureRecords(size_t bytes) {
            Rhi::EnsureComputeBuffer(records, device_context.GetAllocatorState(), bytes, false, "SumByKey Records");
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
        vk::CommandBuffer cb,
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

        // The whole level chain is a pure function of this call's capacity, and the
        // record-region partition is a pure function of the capacity, the channel
        // count and the device's descriptor offset alignment.
        const LevelGeometry geometry = ComputeGeometry(capacity);
        const uint32_t k = static_cast<uint32_t>(geometry.r.size());
        if (k == 0u) return;

        const size_t alignment = std::max<size_t>(
            1u,
            m_impl->device_context.GetDeviceInterface().QueryLimit(
                Rhi::DeviceInterface::PhysicalDeviceLimitInteger::StorageBufferOffsetAlignment
            )
        );
        const RecordLayout layout = ComputeRecordLayout(geometry, num_channels, alignment);

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
        // A single-level reduction has no record regions, so the storage is
        // created at a minimal size and the shader's unused bindings point at the
        // output instead; see the level loop below.
        m_impl->EnsureRecords(std::max<size_t>(layout.total_bytes, sizeof(uint32_t)));
        Rhi::ComputeBuffer &records_buf = *m_impl->records;

        const std::vector<RecordRegion> &regions = layout.regions;

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
                    records_buf, reg.key_offset, static_cast<size_t>(reg.count) * sizeof(uint32_t)
                );
                values_in = Rhi::ComputeKernelResource::Buffer(
                    records_buf, reg.val_offset, static_cast<size_t>(num_channels) * reg.count * sizeof(uint32_t)
                );
            }

            // ---- Write target for this level ----
            uint32_t record_stride = 0u;
            Rhi::ComputeKernelResource rec_keys{};
            Rhi::ComputeKernelResource rec_values{};
            if (level + 1u < k) {
                // Non-final: emit records into region for R_{level+1}.
                const auto &out_reg = regions[level]; // index level == region R_{level+1}
                rec_keys = Rhi::ComputeKernelResource::Buffer(
                    records_buf, out_reg.key_offset, static_cast<size_t>(out_reg.count) * sizeof(uint32_t)
                );
                rec_values = Rhi::ComputeKernelResource::Buffer(
                    records_buf,
                    out_reg.val_offset,
                    static_cast<size_t>(num_channels) * out_reg.count * sizeof(uint32_t)
                );
                record_stride = out_reg.count;
            } else if (k >= 2u) {
                // Final level: no record output. Bind harmless ranges to keep the
                // descriptor set complete.
                const auto &last_reg = regions[k - 2u];
                rec_keys = Rhi::ComputeKernelResource::Buffer(
                    records_buf, last_reg.key_offset, static_cast<size_t>(last_reg.count) * sizeof(uint32_t)
                );
                rec_values = Rhi::ComputeKernelResource::Buffer(
                    records_buf,
                    last_reg.val_offset,
                    static_cast<size_t>(num_channels) * last_reg.count * sizeof(uint32_t)
                );
            } else {
                // Single-level reduction (k == 1): no record regions exist. The
                // shader's final branch never touches RecKeys/RecValues, but the
                // descriptor set must still be complete, so point them at the
                // (guaranteed non-empty) output buffer.
                rec_keys = out_values;
                rec_values = out_values;
            }

            const uint32_t gather_payload = (level == 0u) ? 1u : 0u;
            if (entry_count_buf != nullptr) {
                const SumByKeyPush params{input_count, num_channels, max_key_value, record_stride, gather_payload};
                m_impl->reduce_kernel->Dispatch(
                    cb,
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
                    cb,
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
                DispatchBarrier(cb);
            }
        }
    }
} // namespace Engine
