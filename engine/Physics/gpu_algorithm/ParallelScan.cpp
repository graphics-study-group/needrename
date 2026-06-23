#include "ParallelScan.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Render/Memory/ComputeBuffer.h>
#include <Render/Memory/MemoryAccessTypes.h>
#include <Render/Memory/ShaderParameters/ShaderResourceBinding.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/Compute/ComputeResourceBinding.h>
#include <Render/Pipeline/Compute/ComputeStage.h>
#include <Render/Pipeline/RenderGraph/RGAttachmentDesc.h>
#include <Render/Pipeline/RenderGraph/RenderGraphBuilder.h>
#include <Render/Pipeline/RenderGraph/RenderGraphPass.h>
#include <Render/RenderSystem.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cassert>

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
} // namespace

namespace Engine {

    /// Host-visible buffer content for ScanParams (3 uints: mode, block_offset, elem_count).
    struct alignas(16) ScanParamsGpu {
        uint32_t mode;
        uint32_t block_offset;
        uint32_t elem_count;
        uint32_t _pad;
    };
    static_assert(sizeof(ScanParamsGpu) == 16, "ScanParamsGpu must be 16 bytes");

    static constexpr uint32_t kBlockSize = 512u;  // SHARED_N in shader
    static constexpr uint32_t kMaxSingleLevel = 512u;

    struct ParallelScan::Impl {
        RenderSystem &render_system;
        uint32_t max_elem_count;
        bool initialized = false;

        // ---- Compute stage ----
        std::unique_ptr<ComputeStage> scan_stage;
        std::vector<uint32_t> scan_spirv;

        // ---- Scratch buffer for block sums ----
        // Sized to hold block-sum entries for all recursion levels.
        // Geometric series: M + ceil(M/512) + ceil(ceil(M/512)/512) + ...
        // where M = ceil(max_elem_count / 512).
        uint32_t max_workgroups;
        uint32_t block_sums_entries;   // total entries needed across all levels
        std::unique_ptr<ComputeBuffer> block_sums_buf;

        // ---- Parameter buffer pool ----
        // Each pass gets its own tiny host-visible buffer so that
        // parameters are never overwritten between passes.
        std::vector<std::unique_ptr<ComputeBuffer>> param_pool;

        explicit Impl(RenderSystem &rs, uint32_t mec) :
            render_system(rs), max_elem_count(mec) {
            if (max_elem_count == 0u) {
                throw std::invalid_argument("ParallelScan: max_elem_count must be > 0");
            }
            max_workgroups = (max_elem_count + kBlockSize - 1u) / kBlockSize;

            // Compute total block-sums entries for all recursion levels.
            // Geometric series: M + ceil(M/512) + ceil(ceil(M/512)/512) + ...
            // Stops when level_blocks ≤ 1 (single-workgroup mode-0 scan
            // writes no block sums).  Since 512 ≫ 1 the series converges
            // within 2–3 iterations for any practical M.
            block_sums_entries = 1u;  // at least one entry
            uint32_t level_blocks = max_workgroups;
            while (level_blocks > 1u) {
                block_sums_entries += level_blocks;
                level_blocks = (level_blocks + kBlockSize - 1u) / kBlockSize;
            }
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        // -------------------------------------------------------------------
        // Lazy init
        // -------------------------------------------------------------------

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            const char *scan_path = "algorithm/parallel_scan.comp.spv";
            scan_spirv = LoadPhysicsSpirvBytes(scan_path);
            scan_stage = std::make_unique<ComputeStage>(render_system);
            scan_stage->Instantiate(scan_spirv, "ParallelScan");

            // Allocate block-sums scratch buffer.
            // block_sums_entries includes room for all recursion levels.
            const auto &alloc = render_system.GetAllocatorState();
            block_sums_buf = ComputeBuffer::CreateUnique(
                alloc,
                static_cast<size_t>(block_sums_entries) * sizeof(uint32_t),
                false, false, false, false,
                "ParallelScan BlockSums"
            );
        }

        // -------------------------------------------------------------------
        // Parameter buffer pool
        // -------------------------------------------------------------------

        /// Acquire a fresh host-visible parameter buffer with the given values.
        ComputeBuffer &AcquireParamBuffer(uint32_t mode, uint32_t block_offset, uint32_t elem_count) {
            const auto &alloc = render_system.GetAllocatorState();
            auto buf = ComputeBuffer::CreateUnique(
                alloc,
                sizeof(ScanParamsGpu),
                true, false, false, false,
                "ParallelScan Params"
            );
            auto *addr = reinterpret_cast<ScanParamsGpu *>(buf->GetVMAddress());
            addr->mode = mode;
            addr->block_offset = block_offset;
            addr->elem_count = elem_count;
            addr->_pad = 0u;

            param_pool.push_back(std::move(buf));
            return *param_pool.back();
        }

        // -------------------------------------------------------------------
        // Add a single scan dispatch
        // -------------------------------------------------------------------

        // clang-format off
        /// @param data_input_handle   Handle for input buffer (read).
        /// @param data_output_handle  Handle for output buffer (write).
        /// @param data_input_buf      Input buffer reference.
        /// @param data_output_buf     Output buffer reference.
        /// @param block_sums_handle   Handle for block-sums buffer (write in mode 1, read+write in mode 0, read in mode 2).
        /// @param block_sums_buf      Block-sums buffer reference.
        /// @param param_buf           Per-pass parameter buffer.
        /// @param num_workgroups      Number of workgroups to dispatch.
        // clang-format on
        void AddSinglePass(
            RenderGraphBuilder &builder,
            RGBufferHandle data_input_handle,
            RGBufferHandle data_output_handle,
            ComputeBuffer &data_input_buf,
            ComputeBuffer &data_output_buf,
            RGBufferHandle block_sums_handle,
            ComputeBuffer &block_sums_buf,
            ComputeBuffer &param_buf,
            uint32_t num_workgroups
        ) {
            auto *stage = scan_stage.get();
            auto *binding = &stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();

            srb.BindBuffer("InputData", data_input_buf);
            srb.BindBuffer("OutputData", data_output_buf);
            srb.BindBuffer("ScanParams", param_buf);
            srb.BindBuffer("BlockSums", block_sums_buf);

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
            const MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};
            const MemoryAccessTypeBuffer RRWW{AT::ShaderRandomRead, AT::ShaderRandomWrite};

            bool in_place = (&data_input_buf == &data_output_buf);

            auto pass_builder = RenderGraphPassBuilder{render_system}
                .SetName("ParallelScan")
                .SetAffinity(RenderGraphPassAffinity::Compute);

            if (in_place) {
                pass_builder.UseBuffer(data_input_handle, RRWW);
            } else {
                pass_builder.UseBuffer(data_input_handle, RR);
                pass_builder.UseBuffer(data_output_handle, WW);
            }

            // Block sums: always declare access so barriers are inserted.
            pass_builder.UseBuffer(block_sums_handle, RRWW);

            pass_builder.SetPassFunction(
                [stage, binding, num_workgroups](CommandBuffer &cb, const RenderGraph &) -> void {
                    cb.BindComputeStage(*stage);
                    cb.BindComputeResource(*binding);
                    cb.DispatchCompute(num_workgroups, 1, 1);
                }
            );

            builder.AddPass(pass_builder.Get());
        }

        // -------------------------------------------------------------------
        // Recursive AddScan helper
        // -------------------------------------------------------------------

        /// Add scan passes for `elem_count` elements.
        /// When called recursively, block_sums acts as both data and
        /// block-sum storage (same handle and same buffer).  Sub-block
        /// totals in the recursive level temporarily overwrite the first
        /// few entries of the buffer and are restored by mode-2 add-back.
        void AddScanInternal(
            RenderGraphBuilder &builder,
            RGBufferHandle data_input_handle,
            RGBufferHandle data_output_handle,
            ComputeBuffer &data_input_buf,
            ComputeBuffer &data_output_buf,
            RGBufferHandle block_sums_handle,
            ComputeBuffer &block_sums_buf,
            uint32_t elem_count
        ) {
            assert(elem_count > 0u);
            assert(elem_count <= max_elem_count);

            if (elem_count <= kMaxSingleLevel) {
                // Single-workgroup scan: mode=0.
                auto &param = AcquireParamBuffer(0u, 0u, elem_count);
                AddSinglePass(
                    builder,
                    data_input_handle, data_output_handle,
                    data_input_buf, data_output_buf,
                    block_sums_handle, block_sums_buf,
                    param,
                    1u
                );
                return;
            }

            // Multi-level scan.
            uint32_t num_blocks = (elem_count + kBlockSize - 1u) / kBlockSize;

            // --- Level 1: scan blocks, write per-block sums (mode=1) ---
            {
                auto &param = AcquireParamBuffer(1u, 0u, elem_count);
                AddSinglePass(
                    builder,
                    data_input_handle, data_output_handle,
                    data_input_buf, data_output_buf,
                    block_sums_handle, block_sums_buf,
                    param,
                    num_blocks
                );
            }

            // --- Level 2: recursively scan the block sums ---
            // block_sums_buf is scanned in-place.
            // Use the same handle since it's already imported.
            if (num_blocks <= kMaxSingleLevel) {
                auto &param = AcquireParamBuffer(0u, 0u, num_blocks);
                AddSinglePass(
                    builder,
                    block_sums_handle, block_sums_handle,
                    block_sums_buf, block_sums_buf,
                    block_sums_handle, block_sums_buf,
                    param,
                    1u
                );
            } else {
                // Deep recursion: num_blocks > 512.
                // Recursively scan block_sums[0..num_blocks-1] in-place,
                // using the same buffer for both data and block-sum storage.
                //
                // The recursive scan's level 1 (mode=1) temporarily overwrites
                // block_sums[0..sub_blocks-1] with sub-block totals, and the
                // recursive scan's level 3 (mode=2) restores the correct
                // scanned values for the full range.  Since block_offset=0
                // throughout, the sub-block totals are written to
                // block_sums[0..sub_blocks-1] and are consumed by the
                // recursive level 2/3 before root level 3 reads them.
                //
                // Buffer sizing: block_sums_entries was computed at construction
                // to hold entries for all recursion levels.
                AddScanInternal(
                    builder,
                    block_sums_handle, block_sums_handle,
                    block_sums_buf, block_sums_buf,
                    block_sums_handle, block_sums_buf,
                    num_blocks
                );
            }

            // --- Level 3: add scanned block sums back (mode=2) ---
            {
                auto &param = AcquireParamBuffer(2u, 0u, elem_count);
                AddSinglePass(
                    builder,
                    data_input_handle, data_output_handle,
                    data_input_buf, data_output_buf,
                    block_sums_handle, block_sums_buf,
                    param,
                    num_blocks
                );
            }
        }
    };

    // ===================================================================
    // Public API
    // ===================================================================

    ParallelScan::ParallelScan(RenderSystem &render_system, uint32_t max_elem_count)
        : m_impl(std::make_unique<Impl>(render_system, max_elem_count)) {
    }

    ParallelScan::~ParallelScan() = default;

    bool ParallelScan::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t ParallelScan::GetMaxElemCount() const noexcept {
        return m_impl->max_elem_count;
    }

    void ParallelScan::AddPasses(
        RenderGraphBuilder &builder,
        RGBufferHandle input_handle,
        RGBufferHandle output_handle,
        ComputeBuffer &input_buf,
        ComputeBuffer &output_buf,
        uint32_t elem_count
    ) {
        if (elem_count == 0u) {
            return;  // Nothing to scan.
        }
        if (elem_count > m_impl->max_elem_count) {
            throw std::runtime_error(
                "ParallelScan::AddPasses: elem_count " + std::to_string(elem_count)
                + " exceeds max_elem_count " + std::to_string(m_impl->max_elem_count)
            );
        }

        m_impl->EnsureInitialized();

        // Import the internal block-sums buffer for render-graph tracking.
        using AT = MemoryAccessTypeBufferBits;
        auto block_sums_handle = builder.ImportExternalResource(
            *m_impl->block_sums_buf, {AT::None}
        );

        m_impl->AddScanInternal(
            builder,
            input_handle, output_handle,
            input_buf, output_buf,
            block_sums_handle, *m_impl->block_sums_buf,
            elem_count
        );
    }
} // namespace Engine
