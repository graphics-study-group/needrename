#include "ParallelScan.h"

#include "Physics/PhysicsDispatch.h"
#include "Physics/PhysicsSpirvLoader.h"
#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Pipeline/ComputeKernel.h"

#include <array>
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
        /// Each level writes its sub-block totals into the block-sums buffer of
        /// its parity, at an offset after that buffer's previous levels.
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

        /// Two-buffer form: only dispatched when the caller's input and output are different
        /// buffers. Keeps the Vulkan command stream free of a transfer copy.
        Rhi::ComputeKernel *scan_kernel = nullptr;
        /// One-buffer form: the recursion and every in-place caller.
        Rhi::ComputeKernel *scan_inplace_kernel = nullptr;
        /// Offset addition, always in place on the data being scanned.
        Rhi::ComputeKernel *offset_kernel = nullptr;

        /// Two block-sums buffers, alternated by recursion depth.
        ///
        /// One buffer cannot serve every level: a level scans buffer A while writing its own
        /// per-block totals, and WebGPU forbids a dispatch that binds one buffer twice when
        /// either binding is writable. Alternating makes every level's data and block sums
        /// distinct buffers, at the cost of one extra small allocation.
        std::array<std::unique_ptr<Rhi::ComputeBuffer>, 2> block_sums{};

        /// Next free element of each block-sums buffer, reset per `Record` call.
        std::array<uint32_t, 2> block_sums_cursor{0u, 0u};

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
            scan_inplace_kernel =
                &LoadPhysicsKernel(device_context, "algorithm/parallel_scan_inplace.comp.spv", "ParallelScanInPlace");
            offset_kernel = &LoadPhysicsKernel(device_context, "algorithm/add_block_offset.comp.spv", "AddBlockOffset");
        }

        /// @brief Grow the block-sums storage to cover a scan of @p elem_count.
        void EnsureBlockSums(uint32_t elem_count) {
            for (size_t index = 0; index < block_sums.size(); ++index) {
                Rhi::EnsureComputeBuffer(
                    block_sums[index],
                    device_context.GetAllocatorState(),
                    BlockSumsBytes(elem_count),
                    false,
                    index == 0 ? "ParallelScan BlockSums 0" : "ParallelScan BlockSums 1"
                );
            }
        }

        /// @brief Record one in-place scan pass over @p data_buf.
        void RecordInPlacePass(
            Rhi::CommandEncoder &encoder,
            Rhi::ComputeKernel &kernel,
            Rhi::ComputeBuffer &data_buf,
            Rhi::ComputeBuffer &block_sums_buf,
            const ScanParamsPush &params,
            uint32_t num_workgroups
        ) {
            kernel.Dispatch(encoder, {{"Data", data_buf}, {"BlockSums", block_sums_buf}}, num_workgroups, 1, 1, params);
        }

        /// @brief Record one two-buffer scan pass over @p data_input_buf -> @p data_output_buf.
        void RecordTwoBufferPass(
            Rhi::CommandEncoder &encoder,
            Rhi::ComputeBuffer &data_input_buf,
            Rhi::ComputeBuffer &data_output_buf,
            Rhi::ComputeBuffer &block_sums_buf,
            const ScanParamsPush &params,
            uint32_t num_workgroups
        ) {
            scan_kernel->Dispatch(
                encoder,
                {{"InputData", data_input_buf}, {"OutputData", data_output_buf}, {"BlockSums", block_sums_buf}},
                num_workgroups,
                1,
                1,
                params
            );
        }

        /// @brief Record the offset-addition pass, which is always in place on @p data_buf.
        void RecordOffsetPass(
            Rhi::CommandEncoder &encoder,
            Rhi::ComputeBuffer &data_buf,
            Rhi::ComputeBuffer &block_sums_buf,
            const ScanParamsPush &params,
            uint32_t num_workgroups
        ) {
            offset_kernel->Dispatch(
                encoder, {{"Data", data_buf}, {"BlockSums", block_sums_buf}}, num_workgroups, 1, 1, params
            );
        }

        /// @brief Scan @p data_buf in place, recursing over the block totals.
        ///
        /// @p depth selects the parity of the block-sums buffer - see `block_sums`.
        void RecordInPlaceScanInternal(
            Rhi::CommandEncoder &encoder,
            Rhi::ComputeBuffer &data_buf,
            uint32_t elem_count,
            uint32_t data_offset,
            size_t depth
        ) {
            assert(elem_count > 0u);
            Rhi::ComputeBuffer &block_sums_buf = *block_sums[depth & 1u];

            if (elem_count <= kMaxSingleLevel) {
                RecordInPlacePass(
                    encoder,
                    *scan_inplace_kernel,
                    data_buf,
                    block_sums_buf,
                    ScanParamsPush{0u, data_offset, elem_count, 0u},
                    1u
                );
                return;
            }

            const uint32_t num_blocks = (elem_count + kBlockSize - 1u) / kBlockSize;
            const uint32_t block_offset = block_sums_cursor[depth & 1u];
            block_sums_cursor[depth & 1u] += num_blocks;

            // --- Pass 1: scan blocks, write per-block sums ---
            RecordInPlacePass(
                encoder,
                *scan_inplace_kernel,
                data_buf,
                block_sums_buf,
                ScanParamsPush{1u, data_offset, elem_count, block_offset},
                num_blocks
            );

            DispatchBarrier(encoder);

            // --- Pass 2: recursively scan the block sums, in place, in the other buffer ---
            RecordInPlaceScanInternal(encoder, block_sums_buf, num_blocks, block_offset, depth + 1u);

            DispatchBarrier(encoder);

            // --- Pass 3: add prefix-summed block offsets back to data ---
            RecordOffsetPass(
                encoder, data_buf, block_sums_buf, ScanParamsPush{2u, data_offset, elem_count, block_offset}, num_blocks
            );
        }

        /// @brief Scan @p input into @p output when the two are different buffers.
        void RecordTwoBufferScanInternal(
            Rhi::CommandEncoder &encoder,
            Rhi::ComputeBuffer &input_buf,
            Rhi::ComputeBuffer &output_buf,
            uint32_t elem_count,
            uint32_t data_offset,
            size_t depth
        ) {
            assert(elem_count > 0u);
            assert(&input_buf != &output_buf);
            Rhi::ComputeBuffer &block_sums_buf = *block_sums[depth & 1u];

            if (elem_count <= kMaxSingleLevel) {
                RecordTwoBufferPass(
                    encoder, input_buf, output_buf, block_sums_buf, ScanParamsPush{0u, data_offset, elem_count, 0u}, 1u
                );
                return;
            }

            const uint32_t num_blocks = (elem_count + kBlockSize - 1u) / kBlockSize;
            const uint32_t block_offset = block_sums_cursor[depth & 1u];
            block_sums_cursor[depth & 1u] += num_blocks;

            RecordTwoBufferPass(
                encoder,
                input_buf,
                output_buf,
                block_sums_buf,
                ScanParamsPush{1u, data_offset, elem_count, block_offset},
                num_blocks
            );

            DispatchBarrier(encoder);

            // The block totals are scanned in place, which is the one-buffer form.
            RecordInPlaceScanInternal(encoder, block_sums_buf, num_blocks, block_offset, depth + 1u);

            DispatchBarrier(encoder);

            RecordOffsetPass(
                encoder,
                output_buf,
                block_sums_buf,
                ScanParamsPush{2u, data_offset, elem_count, block_offset},
                num_blocks
            );
        }

        /// @brief Record a scan of @p input_buf into @p output_buf, in place when they alias.
        void RecordScan(
            Rhi::CommandEncoder &encoder,
            Rhi::ComputeBuffer &input_buf,
            Rhi::ComputeBuffer &output_buf,
            uint32_t elem_count
        ) {
            block_sums_cursor = {0u, 0u};
            if (&input_buf == &output_buf) {
                RecordInPlaceScanInternal(encoder, output_buf, elem_count, 0u, 0u);
            } else {
                RecordTwoBufferScanInternal(encoder, input_buf, output_buf, elem_count, 0u, 0u);
            }
        }
    };

    ParallelScan::ParallelScan(Rhi::DeviceContext &device_context) : m_impl(std::make_unique<Impl>(device_context)) {
    }

    ParallelScan::~ParallelScan() = default;

    void ParallelScan::Record(
        Rhi::CommandEncoder &encoder, Rhi::ComputeBuffer &input_buf, Rhi::ComputeBuffer &output_buf, uint32_t elem_count
    ) {
        if (elem_count == 0u) {
            return;
        }

        m_impl->EnsureInitialized();
        m_impl->EnsureBlockSums(elem_count);
        m_impl->RecordScan(encoder, input_buf, output_buf, elem_count);
    }
} // namespace Engine
