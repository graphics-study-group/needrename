#include "CompactUnique.h"

#include <vulkan/vulkan.hpp>

#include "ParallelScan.h"
#include "Physics/PhysicsDispatch.h"
#include "Physics/PhysicsSpirvLoader.h"
#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Pipeline/ComputeKernel.h"

namespace Engine {

    struct CompactUnique::Impl {
        Rhi::DeviceContext &device_context;
        bool initialized = false;

        Rhi::ComputeKernel *flag_kernel = nullptr;
        Rhi::ComputeKernel *scatter_kernel = nullptr;
        Rhi::ComputeKernel *copy_kernel = nullptr;
        Rhi::ComputeKernel *memset_kernel = nullptr;

        // Owned working storage, grown geometrically and reused across calls: the
        // unique-flag array, the flag offsets the scan writes, and the prefix-sum
        // executor, which owns its own block sums.
        std::unique_ptr<ParallelScan> scan{};
        std::unique_ptr<Rhi::ComputeBuffer> flags{};
        std::unique_ptr<Rhi::ComputeBuffer> offsets{};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
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
            scan = std::make_unique<ParallelScan>(device_context);
        }

        /// @brief Grow the working storage to hold @p elem_capacity keys.
        void EnsureWorkingStorage(uint32_t elem_capacity) {
            const size_t bytes = static_cast<size_t>(elem_capacity) * sizeof(uint32_t);
            const Rhi::AllocatorState &allocator = device_context.GetAllocatorState();
            Rhi::EnsureComputeBuffer(flags, allocator, bytes, false, "CompactUnique Flags");
            Rhi::EnsureComputeBuffer(offsets, allocator, bytes, false, "CompactUnique Offsets");
        }
    };

    CompactUnique::CompactUnique(Rhi::DeviceContext &device_context) : m_impl(std::make_unique<Impl>(device_context)) {
    }

    CompactUnique::~CompactUnique() = default;

    bool CompactUnique::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    void CompactUnique::Record(
        vk::CommandBuffer cb,
        Rhi::ComputeBuffer &keys_buf,
        Rhi::ComputeBuffer &count_buf,
        Rhi::ComputeBuffer &elem_count_buf,
        uint32_t elem_capacity
    ) {
        if (elem_capacity == 0u) {
            return;
        }

        m_impl->EnsureInitialized();
        m_impl->EnsureWorkingStorage(elem_capacity);

        Rhi::ComputeBuffer &flags_buf = *m_impl->flags;
        Rhi::ComputeBuffer &offsets_buf = *m_impl->offsets;

        // 1. Flag unique, 2. copy the flags into the scan's input, 3. scan them
        // into compacted positions, 4. clear the count, 5. scatter into place.
        const uint32_t wg = (elem_capacity + 63u) / 64u;
        m_impl->flag_kernel->Dispatch(
            cb, {{"SortedKeys", keys_buf}, {"UniqueFlags", flags_buf}, {"ElemCount", elem_count_buf}}, wg, 1, 1
        );
        DispatchBarrier(cb);

        m_impl->copy_kernel->Dispatch(
            cb, {{"SrcBuffer", flags_buf}, {"DstBuffer", offsets_buf}, {"ElemCount", elem_count_buf}}, wg, 1, 1
        );
        DispatchBarrier(cb);

        m_impl->scan->Record(cb, offsets_buf, offsets_buf, elem_capacity);
        DispatchBarrier(cb);

        m_impl->memset_kernel->Dispatch(cb, {{"Target", count_buf}}, 1, 1, 1, 1u);
        DispatchBarrier(cb);

        m_impl->scatter_kernel->Dispatch(
            cb,
            {{"SortedKeys", keys_buf},
             {"CompactKeys", keys_buf},
             {"OriginalFlags", flags_buf},
             {"FlagOffsets", offsets_buf},
             {"UniqueCount", count_buf},
             {"ElemCount", elem_count_buf}},
            wg,
            1,
            1
        );
    }
} // namespace Engine
