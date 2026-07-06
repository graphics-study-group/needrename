#include "CompactUnique.h"
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

    struct CompactUnique::Impl {
        RenderSystem &render_system;
        uint32_t max_elem_count = 1u;
        bool initialized = false;

        // ---- Compute stages ----
        std::unique_ptr<ComputeStage> flag_stage{}; // flag_unique.comp
        std::vector<uint32_t> flag_spirv{};

        std::unique_ptr<ComputeStage> scatter_stage{}; // compact_scatter.comp
        std::vector<uint32_t> scatter_spirv{};

        std::unique_ptr<ComputeStage> copy_stage{}; // copy_uint.comp (reused)
        std::vector<uint32_t> copy_spirv{};

        std::unique_ptr<ComputeStage> memset_stage{}; // memset_uint.comp (reused)
        std::vector<uint32_t> memset_spirv{};

        // ---- Constant buffers ----
        std::unique_ptr<ComputeBuffer> gpu_const_one{};

        explicit Impl(RenderSystem &rs, uint32_t mec) : render_system(rs), max_elem_count(mec) {
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

            const char *flag_path = "algorithm/flag_unique.comp.spv";
            flag_spirv = LoadPhysicsSpirvBytes(flag_path);
            flag_stage = std::make_unique<ComputeStage>(render_system);
            flag_stage->Instantiate(flag_spirv, "FlagUnique");

            const char *scatter_path = "algorithm/compact_scatter.comp.spv";
            scatter_spirv = LoadPhysicsSpirvBytes(scatter_path);
            scatter_stage = std::make_unique<ComputeStage>(render_system);
            scatter_stage->Instantiate(scatter_spirv, "CompactScatter");

            const char *copy_path = "collision/SpatialHashBroadDetector/copy_uint.comp.spv";
            copy_spirv = LoadPhysicsSpirvBytes(copy_path);
            copy_stage = std::make_unique<ComputeStage>(render_system);
            copy_stage->Instantiate(copy_spirv, "CompactUnique Copy");

            const char *memset_path = "collision/SpatialHashBroadDetector/memset_uint.comp.spv";
            memset_spirv = LoadPhysicsSpirvBytes(memset_path);
            memset_stage = std::make_unique<ComputeStage>(render_system);
            memset_stage->Instantiate(memset_spirv, "CompactUnique Memset");

            // Create constant buffers.
            const auto &alloc = render_system.GetAllocatorState();
            gpu_const_one =
                ComputeBuffer::CreateUnique(alloc, sizeof(uint32_t), true, false, false, false, "CompactUnique Const1");
            *reinterpret_cast<uint32_t *>(gpu_const_one->GetVMAddress()) = 1u;
        }

        void AddFlagPass(
            RenderGraphBuilder &builder,
            RGBufferHandle pairs_handle,
            ComputeBuffer &pairs_buf,
            RGBufferHandle flags_handle,
            ComputeBuffer &flags_buf,
            RGBufferHandle pair_count_handle,
            ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        ) {
            ComputeResourceBinding &bind = flag_stage->AllocateResourceBinding();
            auto &srb = bind.GetShaderResourceBinding();
            srb.BindBuffer("SortedPairs", pairs_buf);
            srb.BindBuffer("UniqueFlags", flags_buf);
            srb.BindBuffer("ElemCount", pair_count_buf);

            auto *stage = flag_stage.get();
            auto *binding_ptr = &bind;
            uint32_t wg = (elem_capacity + 63u) / 64u;

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
            const MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};
            const MemoryAccessTypeBuffer RRWW{AT::ShaderRandomRead, AT::ShaderRandomWrite};

            // ElemCount buffer may alias pairs_handle? No, separate.
            // But the RG access types for the elem_count buffer: it's read-only here.
            // We import it via the handle pattern. Since we don't have a separate handle,
            // gpu_elem_count_buf is bound but not tracked via RG. That's fine for small read-only constants.

            builder.AddPass(
                RenderGraphPassBuilder{render_system}
                    .SetName("CompactUnique Flag")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(pairs_handle, RR)
                    .UseBuffer(flags_handle, WW)
                    .UseBuffer(pair_count_handle, RR)
                    .SetPassFunction([stage, binding_ptr, wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        void AddCopyPass(
            RenderGraphBuilder &builder,
            RGBufferHandle src_handle,
            ComputeBuffer &src_buf,
            RGBufferHandle dst_handle,
            ComputeBuffer &dst_buf,
            RGBufferHandle pair_count_handle,
            ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        ) {
            ComputeResourceBinding &bind = copy_stage->AllocateResourceBinding();
            auto &srb = bind.GetShaderResourceBinding();
            srb.BindBuffer("SrcBuffer", src_buf);
            srb.BindBuffer("DstBuffer", dst_buf);
            srb.BindBuffer("ElemCount", pair_count_buf);

            auto *stage = copy_stage.get();
            auto *binding_ptr = &bind;
            uint32_t wg = (elem_capacity + 63u) / 64u;

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
            const MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};

            builder.AddPass(
                RenderGraphPassBuilder{render_system}
                    .SetName("CompactUnique Copy Flags")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(src_handle, RR)
                    .UseBuffer(dst_handle, WW)
                    .UseBuffer(pair_count_handle, RR)
                    .SetPassFunction([stage, binding_ptr, wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        void AddClearCountPass(RenderGraphBuilder &builder, RGBufferHandle count_handle, ComputeBuffer &count_buf) {
            ComputeResourceBinding &bind = memset_stage->AllocateResourceBinding();
            auto &srb = bind.GetShaderResourceBinding();
            srb.BindBuffer("Target", count_buf);
            srb.BindBuffer("ElemCount", *gpu_const_one);

            auto *stage = memset_stage.get();
            auto *binding_ptr = &bind;

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};

            builder.AddPass(
                RenderGraphPassBuilder{render_system}
                    .SetName("CompactUnique Clear Count")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(count_handle, WW)
                    .SetPassFunction([stage, binding_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(1, 1, 1);
                    })
                    .Get()
            );
        }

        void AddScatterPass(
            RenderGraphBuilder &builder,
            RGBufferHandle pairs_handle,
            ComputeBuffer &pairs_buf,
            RGBufferHandle flags_handle,
            ComputeBuffer &flags_buf,
            RGBufferHandle offsets_handle,
            ComputeBuffer &offsets_buf,
            RGBufferHandle count_handle,
            ComputeBuffer &count_buf,
            RGBufferHandle pair_count_handle,
            ComputeBuffer &pair_count_buf,
            uint32_t elem_capacity
        ) {
            ComputeResourceBinding &bind = scatter_stage->AllocateResourceBinding();
            auto &srb = bind.GetShaderResourceBinding();
            srb.BindBuffer("SortedPairs", pairs_buf);
            srb.BindBuffer("CompactPairs", pairs_buf); // in-place: read and write same buffer
            srb.BindBuffer("OriginalFlags", flags_buf);
            srb.BindBuffer("FlagOffsets", offsets_buf);
            srb.BindBuffer("UniqueCount", count_buf);
            srb.BindBuffer("ElemCount", pair_count_buf);

            auto *stage = scatter_stage.get();
            auto *binding_ptr = &bind;
            uint32_t wg = (elem_capacity + 63u) / 64u;

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
            const MemoryAccessTypeBuffer RW{AT::ShaderRandomRead, AT::ShaderRandomWrite};

            builder.AddPass(
                RenderGraphPassBuilder{render_system}
                    .SetName("CompactUnique Scatter")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(pairs_handle, RW) // read + write (in-place compact)
                    .UseBuffer(flags_handle, RR)
                    .UseBuffer(offsets_handle, RR)
                    .UseBuffer(count_handle, RW)
                    .UseBuffer(pair_count_handle, RR)
                    .SetPassFunction([stage, binding_ptr, wg](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }
    };

    // ===================================================================
    // Public API
    // ===================================================================

    CompactUnique::CompactUnique(RenderSystem &render_system, uint32_t max_elem_count) :
        m_impl(std::make_unique<Impl>(render_system, max_elem_count)) {
    }

    CompactUnique::~CompactUnique() = default;

    bool CompactUnique::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    uint32_t CompactUnique::GetMaxElemCount() const noexcept {
        return m_impl->max_elem_count;
    }

    void CompactUnique::AddPasses(
        RenderGraphBuilder &builder,
        RGBufferHandle pairs_handle,
        ComputeBuffer &pairs_buf,
        RGBufferHandle flags_handle,
        ComputeBuffer &flags_buf,
        RGBufferHandle offsets_handle,
        ComputeBuffer &offsets_buf,
        RGBufferHandle count_handle,
        ComputeBuffer &count_buf,
        RGBufferHandle scan_scratch_handle,
        ComputeBuffer &scan_scratch_buf,
        ParallelScan &scan,
        RGBufferHandle pair_count_handle,
        ComputeBuffer &pair_count_buf,
        uint32_t elem_capacity
    ) {
        if (elem_capacity == 0u) {
            return;
        }
        if (elem_capacity > m_impl->max_elem_count) {
            throw std::runtime_error(
                "CompactUnique::AddPasses: elem_capacity " + std::to_string(elem_capacity) + " exceeds max_elem_count "
                + std::to_string(m_impl->max_elem_count)
            );
        }

        m_impl->EnsureInitialized();

        // Step 1: Flag unique entries → flags_buf.
        // Binds pair_count_buf to ElemCount — shader reads actual count at GPU execution time.
        m_impl->AddFlagPass(
            builder, pairs_handle, pairs_buf, flags_handle, flags_buf, pair_count_handle, pair_count_buf, elem_capacity
        );

        // Step 2: Copy flags → offsets for scan input.
        m_impl->AddCopyPass(
            builder,
            flags_handle,
            flags_buf,
            offsets_handle,
            offsets_buf,
            pair_count_handle,
            pair_count_buf,
            elem_capacity
        );

        // Step 3: Exclusive prefix sum on offsets (in-place) via external ParallelScan.
        // Scans full elem_capacity; zeros beyond pair_count don't affect the prefix sum result.
        scan.AddPasses(
            builder,
            offsets_handle,
            offsets_handle, // in-place scan
            offsets_buf,
            offsets_buf,
            scan_scratch_handle,
            scan_scratch_buf,
            elem_capacity
        );

        // Step 4: Clear unique count to zero before scatter.
        m_impl->AddClearCountPass(builder, count_handle, count_buf);

        // Step 5: Scatter unique entries → pairs_buf (in-place compact).
        m_impl->AddScatterPass(
            builder,
            pairs_handle,
            pairs_buf,
            flags_handle,
            flags_buf,
            offsets_handle,
            offsets_buf,
            count_handle,
            count_buf,
            pair_count_handle,
            pair_count_buf,
            elem_capacity
        );
    }
} // namespace Engine
