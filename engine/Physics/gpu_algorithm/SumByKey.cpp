#include "SumByKey.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeHelpers.h>

#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Pipeline/ComputeResourceBinding.h>
#include <Rhi/Pipeline/ComputeStage.h>
#include <Rhi/Pipeline/ShaderResourceBinding.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
    std::vector<uint32_t> LoadPhysicsSpirvBytes(const char *relative_path) {
        std::filesystem::path full = std::filesystem::path(ENGINE_PHYSICS_SPIRV_DIR) / relative_path;
        std::ifstream file(full, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open physics SPIR-V: " + full.string());
        }
        const auto size = static_cast<size_t>(file.tellg());
        if (size == 0u || size % sizeof(uint32_t) != 0u) {
            throw std::runtime_error("Invalid physics SPIR-V size: " + full.string());
        }
        std::vector<uint32_t> words(size / sizeof(uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size));
        return words;
    }

    const vk::MemoryBarrier2 kComputeBarrier{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
    };
} // namespace

namespace Engine {

    // Push-constant layout, matching the SumByKeyPush block in
    // engine/Physics/shader/algorithm/sum_by_key.comp (std430).
    struct SumByKeyPush {
        uint32_t input_count;
        uint32_t num_channels;
        uint32_t max_key_value;
        uint32_t record_stride;
        uint32_t gather_pairs;
    };
    static_assert(sizeof(SumByKeyPush) == 20, "SumByKeyPush must be 20 bytes");

    // Level geometry helper shared by the static sizing functions and Impl.
    struct LevelGeometry {
        // R[0] = max_entries; R[i+1] = 2*ceil(R[i]/256) until R.back() <= 256.
        std::vector<uint32_t> r;
    };

    LevelGeometry ComputeGeometry(uint32_t max_entries) {
        LevelGeometry g;
        g.r.push_back(max_entries);
        while (g.r.back() > SumByKey::kBlockSize) {
            uint32_t nb = (g.r.back() + SumByKey::kBlockSize - 1u) / SumByKey::kBlockSize;
            g.r.push_back(2u * nb);
        }
        return g;
    }

    struct SumByKey::Impl {
        Rhi::DeviceContext &device_context;
        uint32_t max_entries = 0u;
        uint32_t max_key_value = 0u;
        uint32_t num_channels = 0u;
        bool initialized = false;

        std::unique_ptr<Rhi::ComputeStage> reduce_stage{};
        std::vector<uint32_t> reduce_spirv{};
        Rhi::ComputeResourceBinding *reduce_binding = nullptr;

        // Number of dispatches == number of level records (R.size()).
        LevelGeometry geometry;
        // For each stored record region (levels 1..k-1): byte offsets into the
        // records buffer for its keys and its values, plus its record count.
        struct Region {
            uint32_t count = 0u; // R_i records
            size_t key_offset = 0u;
            size_t val_offset = 0u;
        };
        std::vector<Region> regions; // regions[0] == region for R_1 (index by (level-1))

        size_t records_bytes = 0u;

        explicit Impl(Rhi::DeviceContext &ctx, uint32_t me, uint32_t mkv, uint32_t nc) :
            device_context(ctx), max_entries(me), max_key_value(mkv), num_channels(nc) {
            if (num_channels == 0u || num_channels > SumByKey::kMaxChannels) {
                throw std::invalid_argument("SumByKey: num_channels must be in [1, kMaxChannels]");
            }
            if (max_entries == 0u) {
                throw std::invalid_argument("SumByKey: max_entries must be > 0");
            }
            if (max_key_value == 0u) {
                throw std::invalid_argument("SumByKey: max_key_value must be > 0");
            }

            geometry = ComputeGeometry(max_entries);

            // Lay out record regions for levels 1..k-1 sequentially.
            const size_t record_scalars = static_cast<size_t>(1u + num_channels); // key + N floats
            size_t cursor = 0u;
            for (size_t i = 1u; i < geometry.r.size(); ++i) {
                Region reg;
                reg.count = geometry.r[i];
                reg.key_offset = cursor;
                reg.val_offset = cursor + static_cast<size_t>(reg.count) * sizeof(uint32_t);
                regions.push_back(reg);
                const size_t region_bytes = static_cast<size_t>(reg.count) * record_scalars * sizeof(uint32_t);
                cursor += region_bytes;
            }
            records_bytes = cursor;
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            const char *path = "algorithm/sum_by_key.comp.spv";
            reduce_spirv = LoadPhysicsSpirvBytes(path);
            reduce_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            reduce_stage->Instantiate(reduce_spirv, "SumByKey");
            reduce_binding = &reduce_stage->AllocateResourceBinding();
        }
    };

    SumByKey::SumByKey(
        Rhi::DeviceContext &device_context, uint32_t max_entries, uint32_t max_key_value, uint32_t num_channels
    ) : m_impl(std::make_unique<Impl>(device_context, max_entries, max_key_value, num_channels)) {
    }

    SumByKey::~SumByKey() = default;

    uint32_t SumByKey::GetNumLevels(uint32_t max_entries) noexcept {
        if (max_entries == 0u) return 0u;
        return static_cast<uint32_t>(ComputeGeometry(max_entries).r.size());
    }

    size_t SumByKey::GetRequiredRecordsBytes(uint32_t max_entries, uint32_t num_channels) noexcept {
        if (max_entries == 0u || num_channels == 0u) return 0u;
        LevelGeometry g = ComputeGeometry(max_entries);
        size_t total_records = 0u;
        for (size_t i = 1u; i < g.r.size(); ++i) {
            total_records += g.r[i];
        }
        return total_records * (sizeof(uint32_t) + num_channels * sizeof(uint32_t));
    }

    bool SumByKey::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t SumByKey::GetMaxEntries() const noexcept {
        return m_impl->max_entries;
    }

    uint32_t SumByKey::GetMaxKeyValue() const noexcept {
        return m_impl->max_key_value;
    }

    uint32_t SumByKey::GetNumChannels() const noexcept {
        return m_impl->num_channels;
    }

    uint32_t SumByKey::GetNumLevels() const noexcept {
        return static_cast<uint32_t>(m_impl->geometry.r.size());
    }

    void SumByKey::Record(
        vk::CommandBuffer cb,
        Rhi::ComputeBuffer &pairs_in_buf,
        Rhi::ComputeBuffer &values_in_buf,
        Rhi::ComputeBuffer &records_buf,
        Rhi::ComputeBuffer &out_values_buf
    ) {
        m_impl->EnsureInitialized();
        const uint32_t k = static_cast<uint32_t>(m_impl->geometry.r.size());
        if (k == 0u) return;

        auto &srb = m_impl->reduce_binding->GetShaderResourceBinding();

        // Level 0's input pair array.  Bound once for every level: it is the same
        // buffer throughout and is only read where `gather_pairs` is set (for an
        // untouched descriptor the shader never dereferences it).
        srb.BindBuffer("PairsIn", pairs_in_buf, 0u, static_cast<size_t>(m_impl->max_entries) * 2u * sizeof(uint32_t));

        // Output value buffer: channel-major with stride max_key_value.
        srb.BindBuffer(
            "OutValues",
            out_values_buf,
            0u,
            static_cast<size_t>(m_impl->num_channels) * m_impl->max_key_value * sizeof(uint32_t)
        );

        for (uint32_t level = 0u; level < k; ++level) {
            const uint32_t input_count = m_impl->geometry.r[level];
            const uint32_t num_blocks = (input_count + kBlockSize - 1u) / kBlockSize;

            // ---- Read source for this level ----
            if (level == 0u) {
                // Level 0 gathers from the pair array, so KeysIn is not read.
                // It is still bound to a harmless range to keep the descriptor
                // set complete, exactly as RecKeys/RecValues are below.
                srb.BindBuffer("KeysIn", pairs_in_buf, 0u, static_cast<size_t>(m_impl->max_entries) * sizeof(uint32_t));
                srb.BindBuffer(
                    "ValuesIn",
                    values_in_buf,
                    0u,
                    static_cast<size_t>(m_impl->num_channels) * m_impl->max_entries * sizeof(uint32_t)
                );
            } else {
                const auto &reg = m_impl->regions[level - 1u];
                srb.BindBuffer(
                    "KeysIn", records_buf, reg.key_offset, static_cast<size_t>(reg.count) * sizeof(uint32_t)
                );
                srb.BindBuffer(
                    "ValuesIn",
                    records_buf,
                    reg.val_offset,
                    static_cast<size_t>(m_impl->num_channels) * reg.count * sizeof(uint32_t)
                );
            }

            // ---- Write target for this level ----
            uint32_t record_stride = 0u;
            if (level + 1u < k) {
                // Non-final: emit records into region for R_{level+1}.
                const auto &out_reg = m_impl->regions[level]; // index level == region R_{level+1}
                srb.BindBuffer(
                    "RecKeys", records_buf, out_reg.key_offset, static_cast<size_t>(out_reg.count) * sizeof(uint32_t)
                );
                srb.BindBuffer(
                    "RecValues",
                    records_buf,
                    out_reg.val_offset,
                    static_cast<size_t>(m_impl->num_channels) * out_reg.count * sizeof(uint32_t)
                );
                record_stride = out_reg.count;
            } else if (k >= 2u) {
                // Final level: no record output. Bind harmless ranges to keep the
                // descriptor set complete.
                const auto &last_reg = m_impl->regions[k - 2u];
                srb.BindBuffer(
                    "RecKeys", records_buf, last_reg.key_offset, static_cast<size_t>(last_reg.count) * sizeof(uint32_t)
                );
                srb.BindBuffer(
                    "RecValues",
                    records_buf,
                    last_reg.val_offset,
                    static_cast<size_t>(m_impl->num_channels) * last_reg.count * sizeof(uint32_t)
                );
            } else {
                // Single-level reduction (k == 1): no record regions exist. The
                // shader's final branch never touches RecKeys/RecValues, but the
                // descriptor set must still be complete, so point them at the
                // (guaranteed non-empty) output buffer.
                srb.BindBuffer(
                    "RecKeys",
                    out_values_buf,
                    0u,
                    static_cast<size_t>(m_impl->num_channels) * m_impl->max_key_value * sizeof(uint32_t)
                );
                srb.BindBuffer(
                    "RecValues",
                    out_values_buf,
                    0u,
                    static_cast<size_t>(m_impl->num_channels) * m_impl->max_key_value * sizeof(uint32_t)
                );
            }

            const uint32_t gather_pairs = (level == 0u) ? 1u : 0u;
            const SumByKeyPush params{
                input_count, m_impl->num_channels, m_impl->max_key_value, record_stride, gather_pairs
            };
            Rhi::PushConstants(cb, *m_impl->reduce_stage, params);
            Rhi::BindComputeStage(cb, *m_impl->reduce_stage);
            Rhi::BindComputeResource(cb, *m_impl->reduce_stage, *m_impl->reduce_binding);
            Rhi::DispatchCompute(cb, num_blocks, 1, 1);

            if (level + 1u < k) {
                cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});
            }
        }
    }
} // namespace Engine
