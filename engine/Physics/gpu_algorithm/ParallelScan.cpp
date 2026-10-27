#include "ParallelScan.h"

#include <vulkan/vulkan.hpp>

#include "Physics/PhysicsDispatch.h"
#include "Physics/PhysicsSpirvLoader.h"
#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Pipeline/ComputeKernel.h"

#include <cassert>

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

    namespace {
        /// @brief Bytes of block sums the whole recursion needs for @p elem_count.
        ///
        /// Accounts for all recursion levels: `(sum_i B_i) * sizeof(uint32_t)`
        /// where `B_0 = ceil(elem_count/512)` and `B_{i+1} = ceil(B_i/512)`
        /// until `B_k <= 512` (the final level uses mode=0, no sub-block sums).
        /// Each level writes its sub-block totals into a region after the parent
        /// level's, so no level aliases another's storage.
        size_t BlockSumsBytes(uint32_t elem_count) noexcept {
            if (elem_count == 0u) return sizeof(uint32_t);
            size_t total_entries = 0u;
            uint32_t n = (elem_count + kBlockSize - 1u) / kBlockSize;
            while (n > 0u) {
                total_entries += n;
                if (n <= kMaxSingleLevel) break;
                n = (n + kBlockSize - 1u) / kBlockSize;
            }
            return total_entries * sizeof(uint32_t);
        }
    } // namespace

    struct ParallelScan::Impl {
        Rhi::DeviceContext &device_context;
        bool initialized = false;

        Rhi::ComputeKernel *scan_kernel = nullptr;
        Rhi::ComputeKernel *offset_kernel = nullptr;

        /// Owned block-sums storage, grown geometrically and reused across calls.
        std::unique_ptr<Rhi::ComputeBuffer> block_sums{};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
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

        /// @brief Grow the block-sums storage to cover a scan of @p elem_count.
        void EnsureBlockSums(uint32_t elem_count) {
            Rhi::EnsureComputeBuffer(
                block_sums,
                device_context.GetAllocatorState(),
                BlockSumsBytes(elem_count),
                false,
                "ParallelScan BlockSums"
            );
        }

        /// @brief Record one pass over the whole block-sums buffer.
        ///
        /// The scan and the offset-add shader declare the same interface, so which
        /// one runs is the only difference between passes; `mode` selects the
        /// behaviour within the scan shader.
        void RecordPass(
            vk::CommandBuffer cb,
            Rhi::ComputeKernel &kernel,
            Rhi::ComputeBuffer &data_input_buf,
            Rhi::ComputeBuffer &data_output_buf,
            const ScanParamsPush &params,
            uint32_t num_workgroups
        ) {
            kernel.Dispatch(
                cb,
                {{"InputData", data_input_buf}, {"OutputData", data_output_buf}, {"BlockSums", *block_sums}},
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
            uint32_t elem_count,
            uint32_t data_offset,
            uint32_t block_offset
        ) {
            assert(elem_count > 0u);

            if (elem_count <= kMaxSingleLevel) {
                RecordPass(
                    cb,
                    *scan_kernel,
                    data_input_buf,
                    data_output_buf,
                    ScanParamsPush{0u, data_offset, elem_count, block_offset},
                    1u
                );
                return;
            }

            uint32_t num_blocks = (elem_count + kBlockSize - 1u) / kBlockSize;

            // --- Pass 1: scan blocks, write per-block sums ---
            RecordPass(
                cb,
                *scan_kernel,
                data_input_buf,
                data_output_buf,
                ScanParamsPush{1u, data_offset, elem_count, block_offset},
                num_blocks
            );

            DispatchBarrier(cb);

            // --- Pass 2: recursively scan the block sums ---
            // The recursion reads and writes the block-sums region itself, as both
            // its data and its own block sums; every push-constant offset stays
            // relative to that region, so the region is addressed from element 0.
            if (num_blocks <= kMaxSingleLevel) {
                RecordPass(
                    cb, *scan_kernel, *block_sums, *block_sums, ScanParamsPush{0u, block_offset, num_blocks, 0u}, 1u
                );
            } else {
                RecordScanInternal(cb, *block_sums, *block_sums, num_blocks, block_offset, block_offset + num_blocks);
            }

            DispatchBarrier(cb);

            // --- Pass 3: add prefix-summed block offsets back to data ---
            RecordPass(
                cb,
                *offset_kernel,
                data_output_buf,
                data_output_buf,
                ScanParamsPush{2u, data_offset, elem_count, block_offset},
                num_blocks
            );
        }
    };

    ParallelScan::ParallelScan(Rhi::DeviceContext &device_context) : m_impl(std::make_unique<Impl>(device_context)) {
    }

    ParallelScan::~ParallelScan() = default;

    void ParallelScan::Record(
        vk::CommandBuffer cb, Rhi::ComputeBuffer &input_buf, Rhi::ComputeBuffer &output_buf, uint32_t elem_count
    ) {
        if (elem_count == 0u) {
            return;
        }

        m_impl->EnsureInitialized();
        m_impl->EnsureBlockSums(elem_count);
        m_impl->RecordScanInternal(cb, input_buf, output_buf, elem_count, 0u, 0u);
    }
} // namespace Engine
