#include "RadixSort.h"

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

    /// Host-visible parameter buffer for radix histogram / scatter passes.
    /// Matches the RadixParams UBO layout in radix_histogram.comp and radix_scatter.comp.
    struct alignas(16) RadixSortParamsGpu {
        uint32_t byte_shift;    // 0, 8, 16, or 24
        uint32_t word_select;   // 0 = pair.y (secondary b), 1 = pair.x (primary a)
        uint32_t elem_capacity; // buffer capacity in pairs (for dispatch sizing, bounds check)
        uint32_t _pad;
    };
    static_assert(sizeof(RadixSortParamsGpu) == 16, "RadixSortParamsGpu must be 16 bytes");

    struct RadixSort::Impl {
        RenderSystem &render_system;
        uint32_t max_elem_count = 1u;
        bool initialized = false;

        // ---- Compute stages ----
        std::unique_ptr<ComputeStage> histogram_stage{}; // radix_histogram.comp
        std::vector<uint32_t> histogram_spirv{};

        std::unique_ptr<ComputeStage> prefix_sum_stage{}; // radix_prefix_sum_256.comp
        std::vector<uint32_t> prefix_sum_spirv{};

        std::unique_ptr<ComputeStage> scatter_stage{}; // radix_scatter.comp
        std::vector<uint32_t> scatter_spirv{};

        std::unique_ptr<ComputeStage> memset_stage{}; // memset_uint.comp (reused)
        std::vector<uint32_t> memset_spirv{};

        // ---- Constant buffer for histogram clear ----
        std::unique_ptr<ComputeBuffer> gpu_const_256{};

        // ---- Parameter buffer pools ----
        // Each dispatch gets its own host-visible buffer so CPU writes
        // during RG construction are not overwritten by later passes.
        std::vector<std::unique_ptr<ComputeBuffer>> histogram_param_pool{};
        size_t histogram_param_index = 0;

        std::vector<std::unique_ptr<ComputeBuffer>> scatter_param_pool{};
        size_t scatter_param_index = 0;

        explicit Impl(RenderSystem &rs, uint32_t mec) : render_system(rs), max_elem_count(mec) {
            if (max_elem_count == 0u) {
                throw std::invalid_argument("RadixSort: max_elem_count must be > 0");
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

            const char *histogram_path = "algorithm/radix_histogram.comp.spv";
            histogram_spirv = LoadPhysicsSpirvBytes(histogram_path);
            histogram_stage = std::make_unique<ComputeStage>(render_system);
            histogram_stage->Instantiate(histogram_spirv, "RadixHistogram");

            const char *prefix_sum_path = "algorithm/radix_prefix_sum_256.comp.spv";
            prefix_sum_spirv = LoadPhysicsSpirvBytes(prefix_sum_path);
            prefix_sum_stage = std::make_unique<ComputeStage>(render_system);
            prefix_sum_stage->Instantiate(prefix_sum_spirv, "RadixPrefixSum256");

            const char *scatter_path = "algorithm/radix_scatter.comp.spv";
            scatter_spirv = LoadPhysicsSpirvBytes(scatter_path);
            scatter_stage = std::make_unique<ComputeStage>(render_system);
            scatter_stage->Instantiate(scatter_spirv, "RadixScatter");

            const char *memset_path = "collision/SpatialHashBroadDetector/memset_uint.comp.spv";
            memset_spirv = LoadPhysicsSpirvBytes(memset_path);
            memset_stage = std::make_unique<ComputeStage>(render_system);
            memset_stage->Instantiate(memset_spirv, "RadixMemset");

            // Create the constant-256 buffer for histogram clearing.
            {
                const auto &alloc = render_system.GetAllocatorState();
                gpu_const_256 = ComputeBuffer::CreateUnique(
                    alloc, sizeof(uint32_t), true, false, false, false, "RadixSort Const256"
                );
                auto *addr = reinterpret_cast<uint32_t *>(gpu_const_256->GetVMAddress());
                *addr = RadixSort::kNumBins; // 256
            }
        }

        // -------------------------------------------------------------------
        // Parameter buffer pools
        // -------------------------------------------------------------------

        ComputeBuffer &AcquireHistogramParam(uint32_t byte_shift, uint32_t word_select, uint32_t elem_capacity) {
            if (histogram_param_index >= histogram_param_pool.size()) {
                const auto &alloc = render_system.GetAllocatorState();
                auto buf = ComputeBuffer::CreateUnique(
                    alloc, sizeof(RadixSortParamsGpu), true, false, false, false, "RadixSort HistogramParams"
                );
                histogram_param_pool.push_back(std::move(buf));
            }
            auto &buf = *histogram_param_pool[histogram_param_index++];
            auto *addr = reinterpret_cast<RadixSortParamsGpu *>(buf.GetVMAddress());
            addr->byte_shift = byte_shift;
            addr->word_select = word_select;
            addr->elem_capacity = elem_capacity;
            addr->_pad = 0u;
            return buf;
        }

        ComputeBuffer &AcquireScatterParam(uint32_t byte_shift, uint32_t word_select, uint32_t elem_capacity) {
            if (scatter_param_index >= scatter_param_pool.size()) {
                const auto &alloc = render_system.GetAllocatorState();
                auto buf = ComputeBuffer::CreateUnique(
                    alloc, sizeof(RadixSortParamsGpu), true, false, false, false, "RadixSort ScatterParams"
                );
                scatter_param_pool.push_back(std::move(buf));
            }
            auto &buf = *scatter_param_pool[scatter_param_index++];
            auto *addr = reinterpret_cast<RadixSortParamsGpu *>(buf.GetVMAddress());
            addr->byte_shift = byte_shift;
            addr->word_select = word_select;
            addr->elem_capacity = elem_capacity;
            addr->_pad = 0u;
            return buf;
        }

        // -------------------------------------------------------------------
        // Pass helpers
        // -------------------------------------------------------------------

        void AddClearPass(RenderGraphBuilder &builder, RGBufferHandle scratch_handle, ComputeBuffer &scratch_buf) {
            ComputeResourceBinding &bind = memset_stage->AllocateResourceBinding();
            auto &srb = bind.GetShaderResourceBinding();
            srb.BindBuffer("Target", scratch_buf);
            srb.BindBuffer("ElemCount", *gpu_const_256);

            auto *stage = memset_stage.get();
            auto *binding_ptr = &bind;

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};
            const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};

            uint32_t wg = (RadixSort::kNumBins + 63u) / 64u; // 4 WGs for 256 elements

            builder.AddPass(
                RenderGraphPassBuilder{render_system}
                    .SetName("RadixSort Clear Histogram")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(scratch_handle, WW)
                    .SetPassFunction([stage, binding_ptr, wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        void AddHistogramPass(
            RenderGraphBuilder &builder,
            RGBufferHandle pairs_handle,
            ComputeBuffer &pairs_buf,
            RGBufferHandle scratch_handle,
            ComputeBuffer &scratch_buf,
            ComputeBuffer &param_buf,
            RGBufferHandle pair_count_handle,
            ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        ) {
            ComputeResourceBinding &bind = histogram_stage->AllocateResourceBinding();
            auto &srb = bind.GetShaderResourceBinding();
            srb.BindBuffer("PairsIn", pairs_buf);
            srb.BindBuffer("Histogram", scratch_buf);
            srb.BindBuffer("RadixParams", param_buf);
            srb.BindBuffer("PairCount", pair_count_buf);

            auto *stage = histogram_stage.get();
            auto *binding_ptr = &bind;
            uint32_t wg = (elem_capacity + 63u) / 64u;

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
            const MemoryAccessTypeBuffer RW{AT::ShaderRandomRead, AT::ShaderRandomWrite};

            builder.AddPass(
                RenderGraphPassBuilder{render_system}
                    .SetName("RadixSort Histogram")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(pairs_handle, RR)
                    .UseBuffer(scratch_handle, RW)
                    .UseBuffer(pair_count_handle, RR)
                    .SetPassFunction([stage, binding_ptr, wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        void AddPrefixSumPass(RenderGraphBuilder &builder, RGBufferHandle scratch_handle, ComputeBuffer &scratch_buf) {
            ComputeResourceBinding &bind = prefix_sum_stage->AllocateResourceBinding();
            auto &srb = bind.GetShaderResourceBinding();
            srb.BindBuffer("Histogram", scratch_buf);

            auto *stage = prefix_sum_stage.get();
            auto *binding_ptr = &bind;

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer RW{AT::ShaderRandomRead, AT::ShaderRandomWrite};

            builder.AddPass(
                RenderGraphPassBuilder{render_system}
                    .SetName("RadixSort PrefixSum256")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(scratch_handle, RW)
                    .SetPassFunction([stage, binding_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(1, 1, 1); // single WG of 256 threads
                    })
                    .Get()
            );
        }

        void AddScatterPass(
            RenderGraphBuilder &builder,
            RGBufferHandle pairs_in_handle,
            ComputeBuffer &pairs_in_buf,
            RGBufferHandle pairs_out_handle,
            ComputeBuffer &pairs_out_buf,
            RGBufferHandle scratch_handle,
            ComputeBuffer &scratch_buf,
            ComputeBuffer &param_buf,
            RGBufferHandle pair_count_handle,
            ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        ) {
            ComputeResourceBinding &bind = scatter_stage->AllocateResourceBinding();
            auto &srb = bind.GetShaderResourceBinding();
            srb.BindBuffer("PairsIn", pairs_in_buf);
            srb.BindBuffer("PairsOut", pairs_out_buf);
            srb.BindBuffer("Histogram", scratch_buf);
            srb.BindBuffer("RadixParams", param_buf);
            srb.BindBuffer("PairCount", pair_count_buf);

            auto *stage = scatter_stage.get();
            auto *binding_ptr = &bind;
            uint32_t wg = (elem_capacity + 63u) / 64u;

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
            const MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};
            const MemoryAccessTypeBuffer RW{AT::ShaderRandomRead, AT::ShaderRandomWrite};

            builder.AddPass(
                RenderGraphPassBuilder{render_system}
                    .SetName("RadixSort Scatter")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(pairs_in_handle, RR)
                    .UseBuffer(pairs_out_handle, WW)
                    .UseBuffer(scratch_handle, RW)
                    .UseBuffer(pair_count_handle, RR)
                    .SetPassFunction([stage, binding_ptr, wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        // -------------------------------------------------------------------
        // Single radix pass
        // -------------------------------------------------------------------

        /// Add one radix pass: clear → histogram → prefix sum → scatter.
        /// Returns the actual output handle (may differ from pairs_out_handle if
        /// the scatter actually writes pairs_out; but here we always follow the
        /// same pattern so callers track ping-pong externally).
        void AddRadixPass(
            RenderGraphBuilder &builder,
            RGBufferHandle pairs_in_handle,
            ComputeBuffer &pairs_in_buf,
            RGBufferHandle pairs_out_handle,
            ComputeBuffer &pairs_out_buf,
            RGBufferHandle scratch_handle,
            ComputeBuffer &scratch_buf,
            RGBufferHandle pair_count_handle,
            ComputeBuffer &pair_count_buf,
            uint32_t byte_shift,
            uint32_t word_select,
            uint32_t elem_capacity
        ) {
            // Step 1: Clear histogram to zero.
            AddClearPass(builder, scratch_handle, scratch_buf);

            // Step 2: Build histogram.
            auto &hist_param = AcquireHistogramParam(byte_shift, word_select, elem_capacity);
            AddHistogramPass(
                builder,
                pairs_in_handle,
                pairs_in_buf,
                scratch_handle,
                scratch_buf,
                hist_param,
                pair_count_handle,
                pair_count_buf,
                elem_capacity
            );

            // Step 3: Exclusive prefix sum over 256 histogram entries.
            AddPrefixSumPass(builder, scratch_handle, scratch_buf);

            // Step 4: Scatter elements to output.
            auto &scat_param = AcquireScatterParam(byte_shift, word_select, elem_capacity);
            AddScatterPass(
                builder,
                pairs_in_handle,
                pairs_in_buf,
                pairs_out_handle,
                pairs_out_buf,
                scratch_handle,
                scratch_buf,
                scat_param,
                pair_count_handle,
                pair_count_buf,
                elem_capacity
            );
        }
    };

    // ===================================================================
    // Public API
    // ===================================================================

    RadixSort::RadixSort(RenderSystem &render_system, uint32_t max_elem_count) :
        m_impl(std::make_unique<Impl>(render_system, max_elem_count)) {
    }

    RadixSort::~RadixSort() = default;

    bool RadixSort::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t RadixSort::GetMaxElemCount() const noexcept {
        return m_impl->max_elem_count;
    }

    void RadixSort::ResetGraph() noexcept {
        // Reset param pool cursors — buffers are reused across passes.
        m_impl->histogram_param_index = 0;
        m_impl->scatter_param_index = 0;
    }

    void RadixSort::AddPasses(
        RenderGraphBuilder &builder,
        RGBufferHandle pairs_handle_a,
        RGBufferHandle pairs_handle_b,
        ComputeBuffer &pairs_buf_a,
        ComputeBuffer &pairs_buf_b,
        RGBufferHandle scratch_handle,
        ComputeBuffer &scratch_buf,
        uint32_t elem_capacity,
        RGBufferHandle pair_count_handle,
        ComputeBuffer &pair_count_buf,
        uint32_t max_shape_count
    ) {
        if (elem_capacity == 0u) {
            return;
        }
        if (elem_capacity > m_impl->max_elem_count) {
            throw std::runtime_error(
                "RadixSort::AddPasses: elem_capacity " + std::to_string(elem_capacity) + " exceeds max_elem_count "
                + std::to_string(m_impl->max_elem_count)
            );
        }
        if (max_shape_count > kMaxShapeCount) {
            throw std::runtime_error(
                "RadixSort::AddPasses: max_shape_count " + std::to_string(max_shape_count) + " exceeds kMaxShapeCount "
                + std::to_string(kMaxShapeCount)
            );
        }

        m_impl->EnsureInitialized();

        // Ping-pong state.
        // Passes 0-3 sort by .y (secondary key, word_select=0).
        // Passes 4-7 sort by .x (primary key,   word_select=1).
        //
        // Even pass index → read from A, write to B.  Odd → read from B, write to A.
        // After pass 7 (odd), result is in B.  So we need to check: after 8 passes
        // (0-indexed passes 0..7), pass 7 writes to B.  But we want the result in A.
        // Pass 7 is odd (7 % 2 == 1) → writes to B.  After 8 passes (0-7), the
        // final write was by pass 7 to B.  Since we want the result in A,
        // we need one more copy, OR we adjust so the last pass writes to A.
        //
        // Simpler: make pass 7 write to A.  Pass 0 writes to B, 1→A, 2→B, ..., 7→A.
        // Pattern: even passes write to B (pong), odd passes write to A (ping).
        // After 8 passes (0-7), pass 7 is odd → writes to A.  Result in A. ✓

        for (uint32_t pass = 0; pass < kNumPasses; ++pass) {
            uint32_t byte_shift = (pass % 4u) * 8u;
            uint32_t word_select = pass / 4u; // 0 = .y (passes 0-3), 1 = .x (passes 4-7)

            bool to_b = (pass % 2u) == 0u; // even pass → write to B

            if (to_b) {
                m_impl->AddRadixPass(
                    builder,
                    pairs_handle_a,
                    pairs_buf_a, // read from A
                    pairs_handle_b,
                    pairs_buf_b, // write to B
                    scratch_handle,
                    scratch_buf,
                    pair_count_handle,
                    pair_count_buf,
                    byte_shift,
                    word_select,
                    elem_capacity
                );
            } else {
                m_impl->AddRadixPass(
                    builder,
                    pairs_handle_b,
                    pairs_buf_b, // read from B
                    pairs_handle_a,
                    pairs_buf_a, // write to A
                    scratch_handle,
                    scratch_buf,
                    pair_count_handle,
                    pair_count_buf,
                    byte_shift,
                    word_select,
                    elem_capacity
                );
            }
        }
    }
} // namespace Engine
