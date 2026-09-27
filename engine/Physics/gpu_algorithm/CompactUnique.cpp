#include "CompactUnique.h"

#include <vulkan/vulkan.hpp>

#include "Physics/PhysicsSpirvLoader.h"
#include "Physics/gpu_algorithm/ParallelScan.h"
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

    struct CompactUnique::Impl {
        Rhi::DeviceContext &device_context;
        uint32_t max_elem_count = 1u;
        bool initialized = false;

        Rhi::ComputeKernel *flag_kernel = nullptr;
        Rhi::ComputeKernel *scatter_kernel = nullptr;
        Rhi::ComputeKernel *copy_kernel = nullptr;
        Rhi::ComputeKernel *memset_kernel = nullptr;

        explicit Impl(Rhi::DeviceContext &ctx, uint32_t mec) : device_context(ctx), max_elem_count(mec) {
            if (max_elem_count == 0u) {
                throw std::invalid_argument("CompactUnique: max_elem_count must be > 0");
            }
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            flag_kernel = &LoadPhysicsKernel(device_context, "algorithm/flag_unique.comp.spv", "FlagUnique");
            scatter_kernel = &LoadPhysicsKernel(device_context, "algorithm/compact_scatter.comp.spv", "CompactScatter");
            copy_kernel = &LoadPhysicsKernel(
                device_context, "collision/SpatialHashBroadDetector/copy_uint.comp.spv", "CompactUnique Copy"
            );
            memset_kernel = &LoadPhysicsKernel(
                device_context, "collision/SpatialHashBroadDetector/memset_uint.comp.spv", "CompactUnique Memset"
            );
        }

        void RecordFlagPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &keys_buf,
            Rhi::ComputeBuffer &flags_buf,
            Rhi::ComputeBuffer &elem_count_buf,
            uint32_t elem_capacity
        ) {
            uint32_t wg = (elem_capacity + 63u) / 64u;

            flag_kernel->Dispatch(
                cb,
                {{"SortedKeys", Rhi::ComputeKernelResource::Buffer(keys_buf)},
                 {"UniqueFlags", Rhi::ComputeKernelResource::Buffer(flags_buf)},
                 {"ElemCount", Rhi::ComputeKernelResource::Buffer(elem_count_buf)}},
                wg,
                1,
                1
            );
        }

        void RecordCopyPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &src_buf,
            Rhi::ComputeBuffer &dst_buf,
            Rhi::ComputeBuffer &elem_count_buf,
            uint32_t elem_capacity
        ) {
            uint32_t wg = (elem_capacity + 63u) / 64u;

            copy_kernel->Dispatch(
                cb,
                {{"SrcBuffer", Rhi::ComputeKernelResource::Buffer(src_buf)},
                 {"DstBuffer", Rhi::ComputeKernelResource::Buffer(dst_buf)},
                 {"ElemCount", Rhi::ComputeKernelResource::Buffer(elem_count_buf)}},
                wg,
                1,
                1
            );
        }

        void RecordClearCountPass(vk::CommandBuffer cb, Rhi::ComputeBuffer &count_buf) {
            memset_kernel->Dispatch(cb, {{"Target", Rhi::ComputeKernelResource::Buffer(count_buf)}}, 1, 1, 1, 1u);
        }

        void RecordScatterPass(
            vk::CommandBuffer cb,
            Rhi::ComputeBuffer &keys_buf,
            Rhi::ComputeBuffer &flags_buf,
            Rhi::ComputeBuffer &offsets_buf,
            Rhi::ComputeBuffer &count_buf,
            Rhi::ComputeBuffer &elem_count_buf,
            uint32_t elem_capacity
        ) {
            uint32_t wg = (elem_capacity + 63u) / 64u;

            scatter_kernel->Dispatch(
                cb,
                {{"SortedKeys", Rhi::ComputeKernelResource::Buffer(keys_buf)},
                 {"CompactKeys", Rhi::ComputeKernelResource::Buffer(keys_buf)},
                 {"OriginalFlags", Rhi::ComputeKernelResource::Buffer(flags_buf)},
                 {"FlagOffsets", Rhi::ComputeKernelResource::Buffer(offsets_buf)},
                 {"UniqueCount", Rhi::ComputeKernelResource::Buffer(count_buf)},
                 {"ElemCount", Rhi::ComputeKernelResource::Buffer(elem_count_buf)}},
                wg,
                1,
                1
            );
        }
    };

    CompactUnique::CompactUnique(Rhi::DeviceContext &device_context, uint32_t max_elem_count) :
        m_impl(std::make_unique<Impl>(device_context, max_elem_count)) {
    }

    CompactUnique::~CompactUnique() = default;

    bool CompactUnique::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t CompactUnique::GetMaxElemCount() const noexcept {
        return m_impl->max_elem_count;
    }

    void CompactUnique::Record(
        vk::CommandBuffer cb,
        Rhi::ComputeBuffer &keys_buf,
        Rhi::ComputeBuffer &flags_buf,
        Rhi::ComputeBuffer &offsets_buf,
        Rhi::ComputeBuffer &count_buf,
        Rhi::ComputeBuffer &scan_scratch_buf,
        ParallelScan &scan,
        Rhi::ComputeBuffer &elem_count_buf,
        uint32_t elem_capacity
    ) {
        if (elem_capacity == 0u) {
            return;
        }
        if (elem_capacity > m_impl->max_elem_count) {
            throw std::runtime_error(
                "CompactUnique::Record: elem_capacity " + std::to_string(elem_capacity) + " exceeds max_elem_count "
                + std::to_string(m_impl->max_elem_count)
            );
        }

        m_impl->EnsureInitialized();

        m_impl->RecordFlagPass(cb, keys_buf, flags_buf, elem_count_buf, elem_capacity);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        m_impl->RecordCopyPass(cb, flags_buf, offsets_buf, elem_count_buf, elem_capacity);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        scan.Record(cb, offsets_buf, offsets_buf, scan_scratch_buf, elem_capacity);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        m_impl->RecordClearCountPass(cb, count_buf);
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        m_impl->RecordScatterPass(cb, keys_buf, flags_buf, offsets_buf, count_buf, elem_count_buf, elem_capacity);
    }
} // namespace Engine
