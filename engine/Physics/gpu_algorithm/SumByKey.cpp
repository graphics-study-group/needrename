#include "SumByKey.h"

#include <vulkan/vulkan.hpp>

#include "Physics/PhysicsSpirvLoader.h"
#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Pipeline/ComputeKernel.h"

#include <cassert>
#include <vector>

namespace {
    const vk::MemoryBarrier2 kComputeBarrier{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
    };
} // namespace

namespace Engine {

    // Push-constant layout, matching the SumByKeyPush block in
    // engine/Physics/shader/algorithm/sum_by_key.comp (std430).  The entry count
    // is deliberately absent: it is produced on the GPU and travels in a binding.
    struct SumByKeyPush {
        uint32_t input_count;
        uint32_t num_channels;
        uint32_t max_key_value;
        uint32_t record_stride;
        uint32_t gather_payload;
    };
    static_assert(sizeof(SumByKeyPush) == 20, "SumByKeyPush must be 20 bytes");

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

    struct SumByKey::Impl {
        Rhi::DeviceContext &device_context;
        bool initialized = false;

        Rhi::ComputeKernel *reduce_kernel = nullptr;

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
        }
    };

    SumByKey::SumByKey(Rhi::DeviceContext &device_context) : m_impl(std::make_unique<Impl>(device_context)) {
    }

    SumByKey::~SumByKey() = default;

    uint32_t SumByKey::GetNumLevels(uint32_t capacity) noexcept {
        if (capacity == 0u) return 0u;
        return static_cast<uint32_t>(ComputeGeometry(capacity).r.size());
    }

    size_t SumByKey::GetRequiredRecordsBytes(uint32_t capacity, uint32_t num_channels) noexcept {
        if (capacity == 0u || num_channels == 0u) return 0u;
        LevelGeometry g = ComputeGeometry(capacity);
        size_t total_records = 0u;
        for (size_t i = 1u; i < g.r.size(); ++i) {
            total_records += g.r[i];
        }
        return total_records * (sizeof(uint32_t) + num_channels * sizeof(uint32_t));
    }

    bool SumByKey::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    void SumByKey::Record(
        vk::CommandBuffer cb,
        Rhi::ComputeBuffer &keys_in_buf,
        Rhi::ComputeBuffer &payload_in_buf,
        Rhi::ComputeBuffer &values_in_buf,
        Rhi::ComputeBuffer &records_buf,
        Rhi::ComputeBuffer &out_values_buf,
        Rhi::ComputeBuffer &entry_count_buf,
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

        // The construction-time bound is gone, so the out-of-bounds guard is
        // checked per call against the buffers actually bound for this call.
        const size_t keys_bytes = static_cast<size_t>(capacity) * sizeof(uint32_t);
        const size_t values_bytes = static_cast<size_t>(num_channels) * capacity * sizeof(uint32_t);
        const size_t out_bytes = static_cast<size_t>(num_channels) * max_key_value * sizeof(uint32_t);
        const size_t records_bytes = GetRequiredRecordsBytes(capacity, num_channels);
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
        if (records_buf.GetSize() < records_bytes) {
            throw std::runtime_error("SumByKey::Record: record buffer is smaller than GetRequiredRecordsBytes");
        }
        if (entry_count_buf.GetSize() < sizeof(uint32_t)) {
            throw std::runtime_error("SumByKey::Record: entry-count buffer must hold at least one uint");
        }

        m_impl->EnsureInitialized();

        // The whole level chain is a pure function of this call's capacity.
        const LevelGeometry geometry = ComputeGeometry(capacity);
        const uint32_t k = static_cast<uint32_t>(geometry.r.size());
        if (k == 0u) return;

        // Lay out the record regions for levels 1..k-1 sequentially.
        const size_t record_scalars = static_cast<size_t>(1u + num_channels); // key + N floats
        size_t cursor = 0u;
        std::vector<RecordRegion> regions;
        regions.reserve(geometry.r.size() > 0u ? geometry.r.size() - 1u : 0u);
        for (size_t i = 1u; i < geometry.r.size(); ++i) {
            RecordRegion reg;
            reg.count = geometry.r[i];
            reg.key_offset = cursor;
            reg.val_offset = cursor + static_cast<size_t>(reg.count) * sizeof(uint32_t);
            regions.push_back(reg);
            const size_t region_bytes = static_cast<size_t>(reg.count) * record_scalars * sizeof(uint32_t);
            cursor += region_bytes;
        }

        // The caller's sorted key array and its payload-index array: the same
        // buffers at every level, read only where `gather_payload` is set (for an
        // untouched descriptor the shader never dereferences them).
        Rhi::ComputeKernelResource keys_in = Rhi::ComputeKernelResource::Buffer(keys_in_buf, 0u, keys_bytes);
        Rhi::ComputeKernelResource values_in = Rhi::ComputeKernelResource::Buffer(values_in_buf, 0u, values_bytes);
        Rhi::ComputeKernelResource rec_keys{};
        Rhi::ComputeKernelResource rec_values{};

        for (uint32_t level = 0u; level < k; ++level) {
            const uint32_t input_count = geometry.r[level];
            const uint32_t num_blocks = (input_count + kBlockSize - 1u) / kBlockSize;

            // ---- Read source for this level ----
            if (level == 0u) {
                // Level 0 gathers from the caller's key array and payload-index
                // array; only ValuesIn differs from the level-invariant binding.
                values_in = Rhi::ComputeKernelResource::Buffer(values_in_buf, 0u, values_bytes);
            } else {
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
                rec_keys = Rhi::ComputeKernelResource::Buffer(out_values_buf, 0u, out_bytes);
                rec_values = Rhi::ComputeKernelResource::Buffer(out_values_buf, 0u, out_bytes);
            }

            const uint32_t gather_payload = (level == 0u) ? 1u : 0u;
            const SumByKeyPush params{input_count, num_channels, max_key_value, record_stride, gather_payload};
            m_impl->reduce_kernel->Dispatch(
                cb,
                {{"KeysIn", keys_in},
                 {"ValuesIn", values_in},
                 {"PayloadIn", Rhi::ComputeKernelResource::Buffer(payload_in_buf, 0u, keys_bytes)},
                 {"RecKeys", rec_keys},
                 {"RecValues", rec_values},
                 {"OutValues", Rhi::ComputeKernelResource::Buffer(out_values_buf, 0u, out_bytes)},
                 {"EntryCount", Rhi::ComputeKernelResource::Buffer(entry_count_buf, 0u, sizeof(uint32_t))}},
                num_blocks,
                1,
                1,
                params
            );

            if (level + 1u < k) {
                const vk::MemoryBarrier2 dependencies[1] = {kComputeBarrier};
                cb.pipelineBarrier2(vk::DependencyInfo{vk::DependencyFlags{}, 1, dependencies, 0, nullptr, 0, nullptr});
            }
        }
    }
} // namespace Engine
