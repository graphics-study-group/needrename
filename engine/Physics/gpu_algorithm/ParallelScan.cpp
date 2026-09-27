#include "ParallelScan.h"

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

        Rhi::ComputeKernel *scan_kernel = nullptr;
        Rhi::ComputeKernel *offset_kernel = nullptr;

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

            scan_kernel = &LoadPhysicsKernel(device_context, "algorithm/parallel_scan.comp.spv", "ParallelScan");
            offset_kernel = &LoadPhysicsKernel(device_context, "algorithm/add_block_offset.comp.spv", "AddBlockOffset");
        }

        void RecordScanPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &data_input_buf,
            Rhi::ComputeBuffer &data_output_buf,
            Rhi::ComputeBuffer &block_sums_buf,
            const ScanParamsPush &params,
            uint32_t num_workgroups,
            size_t data_binding_offset,
            size_t block_sums_binding_offset
        ) {
            scan_kernel->Dispatch(
                cb,
                {{"InputData", Rhi::ComputeKernelResource::Buffer(data_input_buf, data_binding_offset)},
                 {"OutputData", Rhi::ComputeKernelResource::Buffer(data_output_buf, data_binding_offset)},
                 {"BlockSums", Rhi::ComputeKernelResource::Buffer(block_sums_buf, block_sums_binding_offset)}},
                num_workgroups,
                1,
                1,
                params
            );
        }

        void RecordOffsetPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &data_input_buf,
            Rhi::ComputeBuffer &data_output_buf,
            Rhi::ComputeBuffer &block_sums_buf,
            const ScanParamsPush &params,
            uint32_t num_workgroups,
            size_t data_binding_offset,
            size_t block_sums_binding_offset
        ) {
            offset_kernel->Dispatch(
                cb,
                {{"InputData", Rhi::ComputeKernelResource::Buffer(data_input_buf, data_binding_offset)},
                 {"OutputData", Rhi::ComputeKernelResource::Buffer(data_output_buf, data_binding_offset)},
                 {"BlockSums", Rhi::ComputeKernelResource::Buffer(block_sums_buf, block_sums_binding_offset)}},
                num_workgroups,
                1,
                1,
                params
            );
        }

        void RecordScanInternal(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &data_input_buf,
            Rhi::ComputeBuffer &data_output_buf,
            Rhi::ComputeBuffer &block_sums_buf,
            uint32_t elem_count,
            uint32_t data_offset,
            uint32_t block_offset,
            size_t data_binding_offset,
            size_t block_sums_binding_offset
        ) {
            assert(elem_count > 0u);
            assert(elem_count <= max_elem_count);

            if (elem_count <= kMaxSingleLevel) {
                const ScanParamsPush params{0u, data_offset, elem_count, block_offset};
                RecordScanPass(
                    cb,
                    data_input_buf,
                    data_output_buf,
                    block_sums_buf,
                    params,
                    1u,
                    data_binding_offset,
                    block_sums_binding_offset
                );
                return;
            }

            uint32_t num_blocks = (elem_count + kBlockSize - 1u) / kBlockSize;

            // --- Pass 1: scan blocks, write per-block sums ---
            {
                const ScanParamsPush params{1u, data_offset, elem_count, block_offset};
                RecordScanPass(
                    cb,
                    data_input_buf,
                    data_output_buf,
                    block_sums_buf,
                    params,
                    num_blocks,
                    data_binding_offset,
                    block_sums_binding_offset
                );
            }

            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // --- Pass 2: recursively scan the block sums ---
            // The recursion reads and writes the block-sums region, so its data
            // views are bound at the block-sums offset: every push-constant offset
            // then stays relative to that region, exactly as it is for a caller
            // that passes a dedicated block-sums buffer.
            if (num_blocks <= kMaxSingleLevel) {
                const ScanParamsPush params{0u, block_offset, num_blocks, 0u};
                RecordScanPass(
                    cb,
                    block_sums_buf,
                    block_sums_buf,
                    block_sums_buf,
                    params,
                    1u,
                    block_sums_binding_offset,
                    block_sums_binding_offset
                );
            } else {
                RecordScanInternal(
                    cb,
                    block_sums_buf,
                    block_sums_buf,
                    block_sums_buf,
                    num_blocks,
                    block_offset,
                    block_offset + num_blocks,
                    block_sums_binding_offset,
                    block_sums_binding_offset
                );
            }

            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // --- Pass 3: add prefix-summed block offsets back to data ---
            {
                const ScanParamsPush params{2u, data_offset, elem_count, block_offset};
                RecordOffsetPass(
                    cb,
                    data_output_buf,
                    data_output_buf,
                    block_sums_buf,
                    params,
                    num_blocks,
                    data_binding_offset,
                    block_sums_binding_offset
                );
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
        uint32_t elem_count,
        size_t block_sums_byte_offset
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

        m_impl->RecordScanInternal(
            cb, input_buf, output_buf, block_sums_buf, elem_count, 0u, 0u, 0u, block_sums_byte_offset
        );
    }
} // namespace Engine
