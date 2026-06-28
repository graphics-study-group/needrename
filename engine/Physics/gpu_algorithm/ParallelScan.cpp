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
} // namespace

namespace Engine {

    /// Host-visible buffer content for ScanParams (4 uints, 16 bytes).
    /// Matches the ScanParams UBO layout in both parallel_scan.comp and add_block_offset.comp.
    struct alignas(16) ScanParamsGpu {
        uint32_t mode;         // 0 = single-pass, 1 = scan + write block totals, 2 = offset addition
        uint32_t data_offset;  // offset (in uints) into InputData / OutputData
        uint32_t elem_count;   // logical element count (before data_offset)
        uint32_t block_offset; // offset (in uints) into BlockSums for reads/writes
    };
    static_assert(sizeof(ScanParamsGpu) == 16, "ScanParamsGpu must be 16 bytes");

    static constexpr uint32_t kBlockSize = 512u; // SHARED_N in shader
    static constexpr uint32_t kMaxSingleLevel = 512u;

    struct ParallelScan::Impl {
        RenderSystem &render_system;
        uint32_t max_elem_count = 1u;
        bool initialized = false;

        // ---- Compute stages ----
        std::unique_ptr<ComputeStage> scan_stage{}; // parallel_scan.comp (modes 0 & 1)
        std::vector<uint32_t> scan_spirv{};

        std::unique_ptr<ComputeStage> offset_stage{}; // add_block_offset.comp
        std::vector<uint32_t> offset_spirv{};

        // ---- Parameter buffer pool ----
        // Each pass gets its own tiny host-visible buffer so that
        // parameters are never overwritten between passes.
        // Buffers are reused across AddPasses calls to avoid unbounded growth.
        std::vector<std::unique_ptr<ComputeBuffer>> param_pool{};
        size_t param_pool_index = 0;

        explicit Impl(RenderSystem &rs, uint32_t mec) : render_system(rs), max_elem_count(mec) {
            if (max_elem_count == 0u) {
                throw std::invalid_argument("ParallelScan: max_elem_count must be > 0");
            }
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        // -------------------------------------------------------------------
        // Lazy init (ComputeStages only — no data buffers)
        // -------------------------------------------------------------------

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            const char *scan_path = "algorithm/parallel_scan.comp.spv";
            scan_spirv = LoadPhysicsSpirvBytes(scan_path);
            scan_stage = std::make_unique<ComputeStage>(render_system);
            scan_stage->Instantiate(scan_spirv, "ParallelScan");

            const char *offset_path = "algorithm/add_block_offset.comp.spv";
            offset_spirv = LoadPhysicsSpirvBytes(offset_path);
            offset_stage = std::make_unique<ComputeStage>(render_system);
            offset_stage->Instantiate(offset_spirv, "AddBlockOffset");
        }

        // -------------------------------------------------------------------
        // Parameter buffer pool
        // -------------------------------------------------------------------

        /// Acquire a host-visible parameter buffer with the given values.
        /// Reuses existing pool buffers when available; only allocates new ones
        /// when the pool is exhausted.
        ComputeBuffer &AcquireParamBuffer(
            uint32_t mode, uint32_t data_offset, uint32_t elem_count, uint32_t block_offset
        ) {
            if (param_pool_index >= param_pool.size()) {
                const auto &alloc = render_system.GetAllocatorState();
                auto buf = ComputeBuffer::CreateUnique(
                    alloc, sizeof(ScanParamsGpu), true, false, false, false, "ParallelScan Params"
                );
                param_pool.push_back(std::move(buf));
            }
            auto &buf = *param_pool[param_pool_index++];
            auto *addr = reinterpret_cast<ScanParamsGpu *>(buf.GetVMAddress());
            addr->mode = mode;
            addr->data_offset = data_offset;
            addr->elem_count = elem_count;
            addr->block_offset = block_offset;
            return buf;
        }

        // -------------------------------------------------------------------
        // Single-pass helpers
        // -------------------------------------------------------------------

        /// Dispatch a scan pass (parallel_scan.comp, mode 0 or 1).
        void AddScanPass(
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

            // Block sums: mode 1 writes, mode 0 only reads (but declare RRWW for simplicity).
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

        /// Dispatch an offset-addition pass (add_block_offset.comp).
        void AddOffsetPass(
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
            auto *stage = offset_stage.get();
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
                                    .SetName("AddBlockOffset")
                                    .SetAffinity(RenderGraphPassAffinity::Compute);

            if (in_place) {
                pass_builder.UseBuffer(data_input_handle, RRWW);
            } else {
                pass_builder.UseBuffer(data_input_handle, RR);
                pass_builder.UseBuffer(data_output_handle, WW);
            }

            // Block sums: read-only in this shader, but declared RRWW so the
            // render graph correctly merges with the data handle when both
            // are the same buffer (recursive level).
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
        ///
        /// @param data_offset   Offset into data input/output buffers where the
        ///                      logical data starts (in uints).
        /// @param block_offset  Offset into BlockSums buffer where this level's
        ///                      per-block totals should be written (mode=1) or
        ///                      read from (offset shader).
        ///
        /// When called recursively on BlockSums, block_offset separates the
        /// sub-block totals from the data region, avoiding aliasing between
        /// data and block-sum storage.
        void AddScanInternal(
            RenderGraphBuilder &builder,
            RGBufferHandle data_input_handle,
            RGBufferHandle data_output_handle,
            ComputeBuffer &data_input_buf,
            ComputeBuffer &data_output_buf,
            RGBufferHandle block_sums_handle,
            ComputeBuffer &block_sums_buf,
            uint32_t elem_count,
            uint32_t data_offset,
            uint32_t block_offset
        ) {
            assert(elem_count > 0u);
            assert(elem_count <= max_elem_count);

            if (elem_count <= kMaxSingleLevel) {
                auto &param = AcquireParamBuffer(0u, data_offset, elem_count, block_offset);
                AddScanPass(
                    builder,
                    data_input_handle,
                    data_output_handle,
                    data_input_buf,
                    data_output_buf,
                    block_sums_handle,
                    block_sums_buf,
                    param,
                    1u
                );
                return;
            }

            uint32_t num_blocks = (elem_count + kBlockSize - 1u) / kBlockSize;

            // --- Pass 1: scan blocks, write per-block sums to BlockSums[block_offset + wg_id] ---
            {
                auto &param = AcquireParamBuffer(1u, data_offset, elem_count, block_offset);
                AddScanPass(
                    builder,
                    data_input_handle,
                    data_output_handle,
                    data_input_buf,
                    data_output_buf,
                    block_sums_handle,
                    block_sums_buf,
                    param,
                    num_blocks
                );
            }

            // --- Pass 2: recursively scan the block sums ---
            //
            // The sub-block totals live at BlockSums[block_offset .. block_offset+num_blocks-1].
            // Sub-scan reads from data_offset=block_offset (the sub-block region).
            // If num_blocks > 512, recurse deeper with shifted offsets.
            if (num_blocks <= kMaxSingleLevel) {
                // Sub-scan: data is at BlockSums[block_offset .. block_offset+num_blocks-1].
                // Use data_offset=block_offset so the sub-scan reads the sub-block totals.
                auto &param = AcquireParamBuffer(0u, block_offset, num_blocks, 0u);
                AddScanPass(
                    builder,
                    block_sums_handle,
                    block_sums_handle,
                    block_sums_buf,
                    block_sums_buf,
                    block_sums_handle,
                    block_sums_buf,
                    param,
                    1u
                );
            } else {
                // Deep recursion: num_blocks > 512.
                // Recurse into the sub-block region:
                //   data_offset = block_offset (sub-block totals are the data)
                //   block_offset = block_offset + num_blocks (sub-sub-block totals go after)
                AddScanInternal(
                    builder,
                    block_sums_handle,
                    block_sums_handle,
                    block_sums_buf,
                    block_sums_buf,
                    block_sums_handle,
                    block_sums_buf,
                    num_blocks,
                    block_offset,
                    block_offset + num_blocks
                );
            }

            // --- Pass 3: add prefix-summed block offsets back to data ---
            {
                auto &param = AcquireParamBuffer(2u, data_offset, elem_count, block_offset);
                AddOffsetPass(
                    builder,
                    data_input_handle,
                    data_output_handle,
                    data_input_buf,
                    data_output_buf,
                    block_sums_handle,
                    block_sums_buf,
                    param,
                    num_blocks
                );
            }
        }
    };

    // ===================================================================
    // Public API
    // ===================================================================

    ParallelScan::ParallelScan(RenderSystem &render_system, uint32_t max_elem_count) :
        m_impl(std::make_unique<Impl>(render_system, max_elem_count)) {
    }

    ParallelScan::~ParallelScan() = default;

    bool ParallelScan::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t ParallelScan::GetMaxElemCount() const noexcept {
        return m_impl->max_elem_count;
    }

    void ParallelScan::ResetGraph() noexcept {
        // Reset param pool cursor — buffers are reused across passes within this call.
        m_impl->param_pool_index = 0;
    }

    size_t ParallelScan::GetRequiredBlockSumsBytes(uint32_t max_elem_count) noexcept {
        if (max_elem_count == 0u) return sizeof(uint32_t);
        size_t total_entries = 0;
        uint32_t n = (max_elem_count + kBlockSize - 1u) / kBlockSize;
        while (n > 0u) {
            total_entries += n;
            if (n <= kMaxSingleLevel) break; // last level uses mode=0, no further sub-block sums
            n = (n + kBlockSize - 1u) / kBlockSize;
        }
        return total_entries * sizeof(uint32_t);
    }

    void ParallelScan::AddPasses(
        RenderGraphBuilder &builder,
        RGBufferHandle input_handle,
        RGBufferHandle output_handle,
        ComputeBuffer &input_buf,
        ComputeBuffer &output_buf,
        RGBufferHandle block_sums_handle,
        ComputeBuffer &block_sums_buf,
        uint32_t elem_count
    ) {
        if (elem_count == 0u) {
            return;
        }
        if (elem_count > m_impl->max_elem_count) {
            throw std::runtime_error(
                "ParallelScan::AddPasses: elem_count " + std::to_string(elem_count) + " exceeds max_elem_count "
                + std::to_string(m_impl->max_elem_count)
            );
        }

        m_impl->EnsureInitialized();

        // The caller owns and manages all buffers — we just orchestrate passes.
        m_impl->AddScanInternal(
            builder,
            input_handle,
            output_handle,
            input_buf,
            output_buf,
            block_sums_handle,
            block_sums_buf,
            elem_count,
            0u, // data_offset = 0 (root level, data starts at beginning of buffers)
            0u  // block_offset = 0 (root level, block sums at beginning of scratch)
        );
    }
} // namespace Engine
