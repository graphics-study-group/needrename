#include "ParallelScan.h"

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

    // Push-constant layout, matching the ScanParamsPush block in
    // engine/Physics/shader/algorithm/ (std430).
    struct ScanParamsPush {
        uint32_t mode;
        uint32_t data_offset;
        uint32_t elem_count;
        uint32_t block_offset;
    };
    static_assert(sizeof(ScanParamsPush) == 16, "ScanParamsPush must be 16 bytes");

    static constexpr uint32_t kBlockSize = 512u;
    static constexpr uint32_t kMaxSingleLevel = 512u;

    struct ParallelScan::Impl {
        Rhi::DeviceContext &device_context;
        uint32_t max_elem_count = 1u;
        bool initialized = false;

        std::unique_ptr<Rhi::ComputeStage> scan_stage{};
        std::vector<uint32_t> scan_spirv{};

        std::unique_ptr<Rhi::ComputeStage> offset_stage{};
        std::vector<uint32_t> offset_spirv{};

        Rhi::ComputeResourceBinding *scan_binding = nullptr;
        Rhi::ComputeResourceBinding *offset_binding = nullptr;

        explicit Impl(Rhi::DeviceContext &ctx, uint32_t mec) : device_context(ctx), max_elem_count(mec) {
            if (max_elem_count == 0u) {
                throw std::invalid_argument("ParallelScan: max_elem_count must be > 0");
            }
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            const char *scan_path = "algorithm/parallel_scan.comp.spv";
            scan_spirv = LoadPhysicsSpirvBytes(scan_path);
            scan_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            scan_stage->Instantiate(scan_spirv, "ParallelScan");
            scan_binding = &scan_stage->AllocateResourceBinding();

            const char *offset_path = "algorithm/add_block_offset.comp.spv";
            offset_spirv = LoadPhysicsSpirvBytes(offset_path);
            offset_stage = std::make_unique<Rhi::ComputeStage>(device_context);
            offset_stage->Instantiate(offset_spirv, "AddBlockOffset");
            offset_binding = &offset_stage->AllocateResourceBinding();
        }

        void RecordScanPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &data_input_buf,
            Rhi::ComputeBuffer &data_output_buf,
            Rhi::ComputeBuffer &block_sums_buf,
            const ScanParamsPush &params,
            uint32_t num_workgroups
        ) {
            auto &srb = scan_binding->GetShaderResourceBinding();
            srb.BindBuffer("InputData", data_input_buf);
            srb.BindBuffer("OutputData", data_output_buf);
            srb.BindBuffer("BlockSums", block_sums_buf);

            Rhi::PushConstants(cb, *scan_stage, params);
            Rhi::BindComputeStage(cb, *scan_stage);
            Rhi::BindComputeResource(cb, *scan_stage, *scan_binding);
            Rhi::DispatchCompute(cb, num_workgroups, 1, 1);
        }

        void RecordOffsetPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &data_input_buf,
            Rhi::ComputeBuffer &data_output_buf,
            Rhi::ComputeBuffer &block_sums_buf,
            const ScanParamsPush &params,
            uint32_t num_workgroups
        ) {
            auto &srb = offset_binding->GetShaderResourceBinding();
            srb.BindBuffer("InputData", data_input_buf);
            srb.BindBuffer("OutputData", data_output_buf);
            srb.BindBuffer("BlockSums", block_sums_buf);

            Rhi::PushConstants(cb, *offset_stage, params);
            Rhi::BindComputeStage(cb, *offset_stage);
            Rhi::BindComputeResource(cb, *offset_stage, *offset_binding);
            Rhi::DispatchCompute(cb, num_workgroups, 1, 1);
        }

        void RecordScanInternal(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &data_input_buf,
            Rhi::ComputeBuffer &data_output_buf,
            Rhi::ComputeBuffer &block_sums_buf,
            uint32_t elem_count,
            uint32_t data_offset,
            uint32_t block_offset
        ) {
            assert(elem_count > 0u);
            assert(elem_count <= max_elem_count);

            if (elem_count <= kMaxSingleLevel) {
                const ScanParamsPush params{0u, data_offset, elem_count, block_offset};
                RecordScanPass(cb, data_input_buf, data_output_buf, block_sums_buf, params, 1u);
                return;
            }

            uint32_t num_blocks = (elem_count + kBlockSize - 1u) / kBlockSize;

            // --- Pass 1: scan blocks, write per-block sums ---
            {
                const ScanParamsPush params{1u, data_offset, elem_count, block_offset};
                RecordScanPass(cb, data_input_buf, data_output_buf, block_sums_buf, params, num_blocks);
            }

            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // --- Pass 2: recursively scan the block sums ---
            if (num_blocks <= kMaxSingleLevel) {
                const ScanParamsPush params{0u, block_offset, num_blocks, 0u};
                RecordScanPass(cb, block_sums_buf, block_sums_buf, block_sums_buf, params, 1u);
            } else {
                RecordScanInternal(
                    cb,
                    block_sums_buf,
                    block_sums_buf,
                    block_sums_buf,
                    num_blocks,
                    block_offset,
                    block_offset + num_blocks
                );
            }

            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // --- Pass 3: add prefix-summed block offsets back to data ---
            {
                const ScanParamsPush params{2u, data_offset, elem_count, block_offset};
                RecordOffsetPass(cb, data_output_buf, data_output_buf, block_sums_buf, params, num_blocks);
            }
        }
    };

    ParallelScan::ParallelScan(Rhi::DeviceContext &device_context, uint32_t max_elem_count) :
        m_impl(std::make_unique<Impl>(device_context, max_elem_count)) {
    }

    ParallelScan::~ParallelScan() = default;

    bool ParallelScan::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t ParallelScan::GetMaxElemCount() const noexcept {
        return m_impl->max_elem_count;
    }

    size_t ParallelScan::GetRequiredBlockSumsBytes(uint32_t max_elem_count) noexcept {
        if (max_elem_count == 0u) return sizeof(uint32_t);
        size_t total_entries = 0;
        uint32_t n = (max_elem_count + kBlockSize - 1u) / kBlockSize;
        while (n > 0u) {
            total_entries += n;
            if (n <= kMaxSingleLevel) break;
            n = (n + kBlockSize - 1u) / kBlockSize;
        }
        return total_entries * sizeof(uint32_t);
    }

    void ParallelScan::Record(
        vk::CommandBuffer cb,
        Rhi::ComputeBuffer &input_buf,
        Rhi::ComputeBuffer &output_buf,
        Rhi::ComputeBuffer &block_sums_buf,
        uint32_t elem_count
    ) {
        if (elem_count == 0u) {
            return;
        }
        if (elem_count > m_impl->max_elem_count) {
            throw std::runtime_error(
                "ParallelScan::Record: elem_count " + std::to_string(elem_count) + " exceeds max_elem_count "
                + std::to_string(m_impl->max_elem_count)
            );
        }

        m_impl->EnsureInitialized();

        m_impl->RecordScanInternal(cb, input_buf, output_buf, block_sums_buf, elem_count, 0u, 0u);
    }
} // namespace Engine
