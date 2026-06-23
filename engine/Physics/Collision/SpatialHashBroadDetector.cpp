#include "SpatialHashBroadDetector.h"

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsScene.h>
#include <Render/Memory/ComputeBuffer.h>
#include <Render/Memory/DeviceBuffer.h>
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

    // ---- GPU-side grid config matching the shader UBO ----
    struct alignas(16) GridConfigGpu {
        glm::vec4 world_min;       // xyz = bounds min, w = cell_size
        glm::ivec4 grid_dims;      // xyz = grid dimensions, w = max_cells_per_shape
        uint32_t total_cells;      // grid_dims.x * grid_dims.y * grid_dims.z
        uint32_t _pad[3];
    };
    static_assert(sizeof(GridConfigGpu) == 48, "GridConfigGpu must match shader UBO layout");

    struct SpatialHashBroadDetector::Impl {
        RenderSystem &render_system;
        GridConfig grid_config;
        uint32_t fallback_threshold;

        bool initialized = false;

        uint32_t cached_shape_count = 0;
        uint32_t max_pairs = 0;
        uint32_t grid_total_cells = 0;
        glm::ivec3 grid_dims{};

        // ---- Compute stages (one per shader) ----
        std::unique_ptr<ComputeStage> aabb_stage;
        ComputeResourceBinding *aabb_binding = nullptr;
        std::vector<uint32_t> aabb_spirv;

        std::unique_ptr<ComputeStage> count_cells_stage;
        ComputeResourceBinding *count_cells_binding = nullptr;
        std::vector<uint32_t> count_cells_spirv;

        std::unique_ptr<ComputeStage> fill_cells_stage;
        ComputeResourceBinding *fill_cells_binding = nullptr;
        std::vector<uint32_t> fill_cells_spirv;

        std::unique_ptr<ComputeStage> histogram_stage;
        ComputeResourceBinding *histogram_binding = nullptr;
        std::vector<uint32_t> histogram_spirv;

        std::unique_ptr<ComputeStage> scatter_sort_stage;
        ComputeResourceBinding *scatter_sort_binding = nullptr;
        std::vector<uint32_t> scatter_sort_spirv;

        std::unique_ptr<ComputeStage> generate_pairs_stage;
        ComputeResourceBinding *generate_pairs_binding = nullptr;
        std::vector<uint32_t> generate_pairs_spirv;

        std::unique_ptr<ComputeStage> fallback_pairs_stage;
        ComputeResourceBinding *fallback_pairs_binding = nullptr;
        std::vector<uint32_t> fallback_pairs_spirv;

        // parallel_scan is reused across multiple passes.
        std::unique_ptr<ComputeStage> scan_stage;
        ComputeResourceBinding *scan_binding = nullptr;
        std::vector<uint32_t> scan_spirv;

        // memset_uint — clears a uint buffer to zero (stage reused, each pass gets own binding).
        std::unique_ptr<ComputeStage> memset_stage;
        std::vector<uint32_t> memset_spirv;

        // copy_uint — copies a uint buffer (stage reused, each pass gets own binding).
        std::unique_ptr<ComputeStage> copy_stage;
        std::vector<uint32_t> copy_spirv;

        // ---- Owned GPU buffers ----

        // Per-shape AABBs.
        std::unique_ptr<ComputeBuffer> gpu_aabb_min;
        std::unique_ptr<ComputeBuffer> gpu_aabb_max;

        // Cell assignment.
        std::unique_ptr<ComputeBuffer> gpu_shape_cell_count;
        std::unique_ptr<ComputeBuffer> gpu_shape_cell_offset;
        std::unique_ptr<ComputeBuffer> gpu_cell_shape_pairs;
        std::unique_ptr<ComputeBuffer> gpu_total_assignments;

        // Counting sort.
        std::unique_ptr<ComputeBuffer> gpu_cell_histogram;
        std::unique_ptr<ComputeBuffer> gpu_cell_offsets;
        std::unique_ptr<ComputeBuffer> gpu_cell_scratch;
        std::unique_ptr<ComputeBuffer> gpu_sorted_pairs;

        // Global flags.
        std::unique_ptr<ComputeBuffer> gpu_global_flags;

        // Output.
        std::unique_ptr<ComputeBuffer> gpu_collision_pairs;
        std::unique_ptr<ComputeBuffer> gpu_pair_count;

        // Grid config UBO.
        std::unique_ptr<ComputeBuffer> gpu_grid_config;

        // Shape slot count (single uint, host-visible).
        std::unique_ptr<ComputeBuffer> gpu_shape_slot_count;

        // Global mode: 0 = all-pairs, 1 = global-only (host-visible).
        std::unique_ptr<ComputeBuffer> gpu_global_mode;

        // Dummy zero uint buffer bound to optional descriptor slots.
        std::unique_ptr<ComputeBuffer> gpu_dummy_uint;

        // Single uint with value 1 — used as ElemCount when clearing single-element buffers.
        std::unique_ptr<ComputeBuffer> gpu_one;

        // Host-visible uint with value = grid_total_cells + 1 — used as ElemCount
        // when clearing the histogram buffer or copying cell offsets.
        std::unique_ptr<ComputeBuffer> gpu_grid_cells_p1;

        // Scan params and element count (host-visible, updated before each scan dispatch).
        std::unique_ptr<ComputeBuffer> gpu_scan_params;
        std::unique_ptr<ComputeBuffer> gpu_scan_elem_count;

        // --- Intermediate buffers for multi-level scan ---
        std::unique_ptr<ComputeBuffer> gpu_scan_block_sums;

        explicit Impl(RenderSystem &rs, uint32_t mp, const GridConfig &gc, uint32_t ft) :
            render_system(rs), max_pairs(mp), grid_config(gc), fallback_threshold(ft) {
            // Validate grid size on construction.
            glm::vec3 extent = gc.world_max - gc.world_min;
            grid_dims.x = static_cast<int>(glm::ceil(extent.x / gc.cell_size));
            grid_dims.y = static_cast<int>(glm::ceil(extent.y / gc.cell_size));
            grid_dims.z = static_cast<int>(glm::ceil(extent.z / gc.cell_size));
            grid_dims = glm::max(grid_dims, glm::ivec3(1));
            uint64_t total = static_cast<uint64_t>(grid_dims.x)
                           * static_cast<uint64_t>(grid_dims.y)
                           * static_cast<uint64_t>(grid_dims.z);
            if (total > (1ull << 20)) {
                throw std::runtime_error(
                    "SpatialHashBroadDetector: grid has " + std::to_string(total)
                    + " cells (> 2^20). Increase cell_size or reduce world bounds."
                );
            }
            grid_total_cells = static_cast<uint32_t>(total);
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        // -------------------------------------------------------------------
        // Buffer helpers
        // -------------------------------------------------------------------

        void EnsureBuffer(std::unique_ptr<ComputeBuffer> &buf, size_t bytes, const char *name, bool host_visible = false) {
            const auto &alloc = render_system.GetAllocatorState();
            if (!buf || buf->GetSize() != bytes) {
                buf = ComputeBuffer::CreateUnique(alloc, bytes, host_visible, false, false, false, name);
            }
        }

        void EnsureAllBuffers(uint32_t shape_count) {
            const auto &alloc = render_system.GetAllocatorState();

            // Single-uint buffer for shape slot count.
            EnsureBuffer(gpu_shape_slot_count, sizeof(uint32_t), "BH ShapeSlotCount", true);

            const size_t shape_vec4 = static_cast<size_t>(shape_count) * sizeof(glm::vec4);
            const size_t shape_uint = static_cast<size_t>(shape_count) * sizeof(uint32_t);

            EnsureBuffer(gpu_aabb_min, shape_vec4, "BH AabbMin");
            EnsureBuffer(gpu_aabb_max, shape_vec4, "BH AabbMax");
            EnsureBuffer(gpu_shape_cell_count, shape_uint, "BH ShapeCellCnt");
            EnsureBuffer(gpu_shape_cell_offset, shape_uint, "BH ShapeCellOff");
            EnsureBuffer(gpu_global_flags, shape_uint, "BH GlobalFlags");

            // Total assignments counter.
            EnsureBuffer(gpu_total_assignments, sizeof(uint32_t), "BH TotalAssign", true);

            // cell_shape_pairs: conservative upper bound.
            uint32_t max_assignments = shape_count * std::max(1u, grid_config.max_cells_per_shape);
            EnsureBuffer(gpu_cell_shape_pairs, static_cast<size_t>(max_assignments) * sizeof(glm::uvec2), "BH CellShapePairs");
            EnsureBuffer(gpu_sorted_pairs, static_cast<size_t>(max_assignments) * sizeof(glm::uvec2), "BH SortedPairs");

            // Counting sort buffers (per-cell).
            size_t cell_uint = static_cast<size_t>(grid_total_cells) * sizeof(uint32_t);
            size_t cell_uint1 = static_cast<size_t>(grid_total_cells + 1u) * sizeof(uint32_t);
            EnsureBuffer(gpu_cell_histogram, cell_uint1, "BH CellHist");
            EnsureBuffer(gpu_cell_offsets, cell_uint1, "BH CellOffsets");
            EnsureBuffer(gpu_cell_scratch, cell_uint1, "BH CellScratch");

            // Output pair buffer — sized to the caller-provided max_pairs.
            size_t pair_bytes = static_cast<size_t>(std::max(1u, max_pairs)) * sizeof(glm::uvec2);
            EnsureBuffer(gpu_collision_pairs, pair_bytes, "BH CollisionPairs");

            // Pair count + zero buffer.
            EnsureBuffer(gpu_pair_count, sizeof(uint32_t), "BH PairCount", true);

            // Global mode buffer (uint, host-visible): 0 = all-pairs, 1 = global-only.
            if (!gpu_global_mode || gpu_global_mode->GetSize() < sizeof(uint32_t)) {
                gpu_global_mode =
                    ComputeBuffer::CreateUnique(alloc, sizeof(uint32_t), true, false, false, false, "BH GlobalMode");
            }

            // Constant-one buffer for memset ElemCount (single-element clears).
            if (!gpu_one || gpu_one->GetSize() < sizeof(uint32_t)) {
                gpu_one =
                    ComputeBuffer::CreateUnique(alloc, sizeof(uint32_t), true, false, false, false, "BH One");
                auto *addr = reinterpret_cast<uint32_t *>(gpu_one->GetVMAddress());
                *addr = 1u;
            }

            // Buffer with value = grid_total_cells + 1 for large clears/copies.
            if (!gpu_grid_cells_p1 || gpu_grid_cells_p1->GetSize() < sizeof(uint32_t)) {
                gpu_grid_cells_p1 =
                    ComputeBuffer::CreateUnique(alloc, sizeof(uint32_t), true, false, false, false, "BH GridCellsP1");
                auto *addr = reinterpret_cast<uint32_t *>(gpu_grid_cells_p1->GetVMAddress());
                *addr = grid_total_cells + 1u;
            }

            // Scan params buffer: {mode, block_offset} (2 uints, host-visible).
            if (!gpu_scan_params || gpu_scan_params->GetSize() < 2u * sizeof(uint32_t)) {
                gpu_scan_params =
                    ComputeBuffer::CreateUnique(alloc, 2u * sizeof(uint32_t), true, false, false, false, "BH ScanParams");
            }

            // Scan element count buffer (single uint, host-visible).
            if (!gpu_scan_elem_count || gpu_scan_elem_count->GetSize() < sizeof(uint32_t)) {
                gpu_scan_elem_count =
                    ComputeBuffer::CreateUnique(alloc, sizeof(uint32_t), true, false, false, false, "BH ScanElemCnt");
            }

            // Dummy zero buffer sized to shape_count for optional bindings.
            // Must be large enough that shader array accesses (e.g. filter_offset.v[i])
            // never read past the end.  All entries are zero.
            {
                const size_t dummy_bytes = static_cast<size_t>(std::max(1u, shape_count)) * sizeof(uint32_t);
                if (!gpu_dummy_uint || gpu_dummy_uint->GetSize() < dummy_bytes) {
                    gpu_dummy_uint =
                        ComputeBuffer::CreateUnique(alloc, dummy_bytes, true, false, false, false, "BH DummyUint");
                    // Zero-fill all entries.
                    auto *addr = reinterpret_cast<uint32_t *>(gpu_dummy_uint->GetVMAddress());
                    std::fill(addr, addr + std::max(1u, shape_count), 0u);
                }
            }

            // Grid config UBO.
            EnsureBuffer(gpu_grid_config, sizeof(GridConfigGpu), "BH GridConfig", true);

            // Block sums buffer for multi-level scan (one uint per potential workgroup).
            // Maximum workgroups used = max(grid_total_cells, max_assignments, shape_count) / 512 + 1.
            uint32_t max_scan_wgs = (std::max({grid_total_cells, max_assignments, shape_count}) + 511u) / 512u + 1u;
            EnsureBuffer(gpu_scan_block_sums, static_cast<size_t>(max_scan_wgs) * sizeof(uint32_t), "BH ScanBlockSums");
        }

        // // -------------------------------------------------------------------
        // // Parallel scan dispatch helper
        // // -------------------------------------------------------------------

        // /// Dispatch a multi-level exclusive scan on @p data_buf (N = @p count elements).
        // /// @p data_buf is read and overwritten in-place.
        // /// @p block_sums_buf is scratch space for intermediate per-block sums.
        // void DispatchParallelScan(
        //     RenderGraphBuilder &builder,
        //     RGBufferHandle data_handle,
        //     ComputeBuffer &data_buf,
        //     ComputeBuffer &block_sums_buf,
        //     uint32_t count
        // ) {
        //     if (count <= 512u) {
        //         // Single-workgroup: one dispatch with mode=0.
        //         AddScanPass(builder, data_handle, data_buf, nullptr, count, 0u, 0u, 1u);
        //         return;
        //     }

        //     const uint32_t kBlockSize = 512u;
        //     uint32_t num_blocks = (count + kBlockSize - 1u) / kBlockSize;

        //     // --- Level 1: scan blocks, write block sums (mode=1) ---
        //     auto block_sums_handle =
        //         builder.ImportExternalResource(block_sums_buf, {MemoryAccessTypeBufferBits::None});
        //     AddScanPass(builder, data_handle, data_buf, &block_sums_buf, count, 1u, 0u, num_blocks);

        //     // --- Level 2: recursively scan the block sums ---
        //     // Import the block_sums buffer as a data buffer for the recursive scan.
        //     auto bs_data_handle =
        //         builder.ImportExternalResource(block_sums_buf, {MemoryAccessTypeBufferBits::None});

        //     if (num_blocks <= 512u) {
        //         AddScanPass(builder, bs_data_handle, block_sums_buf, nullptr, num_blocks, 0u, 0u, 1u);
        //     } else {
        //         // Nested multi-level; we reuse block_sums_buf for the inner scan's
        //         // block sums (writing to the tail, past num_blocks entries).
        //         // For simplicity we allocate a separate temp buffer.  In practice
        //         // num_blocks rarely exceeds 512 for our use cases.
        //         AddScanPass(builder, bs_data_handle, block_sums_buf, nullptr, num_blocks, 0u, 0u,
        //                     (num_blocks + 511u) / 512u);
        //     }

        //     // --- Level 3: add scanned block sums back (mode=2) ---
        //     AddScanPass(builder, data_handle, data_buf, &block_sums_buf, count, 2u, 0u, num_blocks);
        // }

        // /// Add a single scan dispatch.
        // void AddScanPass(
        //     RenderGraphBuilder &builder,
        //     RGBufferHandle data_handle,
        //     ComputeBuffer &data_buf,
        //     ComputeBuffer *block_sums_buf,  // may be null
        //     uint32_t count,
        //     uint32_t mode,
        //     uint32_t block_offset,
        //     uint32_t workgroups
        // ) {
        //     // Write element count to host-visible buffer.
        //     {
        //         auto *addr = reinterpret_cast<uint32_t *>(gpu_scan_elem_count->GetVMAddress());
        //         *addr = count;
        //     }
        //     // Write scan params {mode, block_offset} to host-visible buffer.
        //     {
        //         auto *addr = reinterpret_cast<uint32_t *>(gpu_scan_params->GetVMAddress());
        //         addr[0] = mode;
        //         addr[1] = block_offset;
        //     }

        //     auto *binding = &scan_stage->AllocateResourceBinding();
        //     auto &srb = binding->GetShaderResourceBinding();
        //     // In-place: input and output are the same buffer.
        //     srb.BindBuffer("InputData", data_buf);
        //     srb.BindBuffer("OutputData", data_buf);
        //     srb.BindBuffer("ElemCount", *gpu_scan_elem_count);
        //     srb.BindBuffer("ScanParams", *gpu_scan_params);

        //     // Block sums (only for modes 1 and 2).  Bind dummy if null.
        //     if (block_sums_buf) {
        //         srb.BindBuffer("BlockSums", *block_sums_buf);
        //     } else {
        //         srb.BindBuffer("BlockSums", *gpu_pair_count);
        //     }

        //     auto *stage = scan_stage.get();
        //     builder.AddPass(
        //         RenderGraphPassBuilder{render_system}
        //             .SetName("BroadPhase ParallelScan")
        //             .SetAffinity(RenderGraphPassAffinity::Compute)
        //             .UseBuffer(data_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead, MemoryAccessTypeBufferBits::ShaderRandomWrite})
        //             .SetPassFunction([stage, binding, workgroups](CommandBuffer &cb, const RenderGraph &) -> void {
        //                 cb.BindComputeStage(*stage);
        //                 cb.BindComputeResource(*binding);
        //                 cb.DispatchCompute(workgroups, 1, 1);
        //             })
        //             .Get()
        //     );
        // }

        // -------------------------------------------------------------------
        // Lazy init
        // -------------------------------------------------------------------

        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            const char *base = "solver/SpatialHashBroadDetector/";
            const char *scan_path = "solver/XPBDSolver/parallel_scan.comp.spv";

            aabb_spirv = LoadPhysicsSpirvBytes((std::string(base) + "compute_aabbs.comp.spv").c_str());
            aabb_stage = std::make_unique<ComputeStage>(render_system);
            aabb_stage->Instantiate(aabb_spirv, "BH ComputeAabbs");
            aabb_binding = &aabb_stage->AllocateResourceBinding();

            count_cells_spirv = LoadPhysicsSpirvBytes((std::string(base) + "count_cells.comp.spv").c_str());
            count_cells_stage = std::make_unique<ComputeStage>(render_system);
            count_cells_stage->Instantiate(count_cells_spirv, "BH CountCells");
            count_cells_binding = &count_cells_stage->AllocateResourceBinding();

            fill_cells_spirv = LoadPhysicsSpirvBytes((std::string(base) + "fill_cells.comp.spv").c_str());
            fill_cells_stage = std::make_unique<ComputeStage>(render_system);
            fill_cells_stage->Instantiate(fill_cells_spirv, "BH FillCells");
            fill_cells_binding = &fill_cells_stage->AllocateResourceBinding();

            histogram_spirv = LoadPhysicsSpirvBytes((std::string(base) + "histogram_cells.comp.spv").c_str());
            histogram_stage = std::make_unique<ComputeStage>(render_system);
            histogram_stage->Instantiate(histogram_spirv, "BH HistogramCells");
            histogram_binding = &histogram_stage->AllocateResourceBinding();

            scatter_sort_spirv = LoadPhysicsSpirvBytes((std::string(base) + "scatter_sort.comp.spv").c_str());
            scatter_sort_stage = std::make_unique<ComputeStage>(render_system);
            scatter_sort_stage->Instantiate(scatter_sort_spirv, "BH ScatterSort");
            scatter_sort_binding = &scatter_sort_stage->AllocateResourceBinding();

            generate_pairs_spirv = LoadPhysicsSpirvBytes((std::string(base) + "generate_broad_pairs.comp.spv").c_str());
            generate_pairs_stage = std::make_unique<ComputeStage>(render_system);
            generate_pairs_stage->Instantiate(generate_pairs_spirv, "BH GeneratePairs");
            generate_pairs_binding = &generate_pairs_stage->AllocateResourceBinding();

            fallback_pairs_spirv = LoadPhysicsSpirvBytes((std::string(base) + "generate_all_pairs_fallback.comp.spv").c_str());
            fallback_pairs_stage = std::make_unique<ComputeStage>(render_system);
            fallback_pairs_stage->Instantiate(fallback_pairs_spirv, "BH FallbackPairs");
            fallback_pairs_binding = &fallback_pairs_stage->AllocateResourceBinding();

            scan_spirv = LoadPhysicsSpirvBytes(scan_path);
            scan_stage = std::make_unique<ComputeStage>(render_system);
            scan_stage->Instantiate(scan_spirv, "BH ParallelScan");
            scan_binding = &scan_stage->AllocateResourceBinding();

            const char *memset_path = "solver/SpatialHashBroadDetector/memset_uint.comp.spv";
            memset_spirv = LoadPhysicsSpirvBytes(memset_path);
            memset_stage = std::make_unique<ComputeStage>(render_system);
            memset_stage->Instantiate(memset_spirv, "BH MemsetUint");

            const char *copy_path = "solver/SpatialHashBroadDetector/copy_uint.comp.spv";
            copy_spirv = LoadPhysicsSpirvBytes(copy_path);
            copy_stage = std::make_unique<ComputeStage>(render_system);
            copy_stage->Instantiate(copy_spirv, "BH CopyUint");
        }

        void UpdateGridConfigGpu() {
            GridConfigGpu cfg{};
            cfg.world_min = glm::vec4(grid_config.world_min, grid_config.cell_size);
            cfg.grid_dims = glm::ivec4(grid_dims, static_cast<int>(grid_config.max_cells_per_shape));
            cfg.total_cells = grid_total_cells;
            auto *addr = reinterpret_cast<GridConfigGpu *>(gpu_grid_config->GetVMAddress());
            *addr = cfg;
        }
    };

    // ===================================================================
    // Public API
    // ===================================================================

    SpatialHashBroadDetector::SpatialHashBroadDetector(
        RenderSystem &render_system, uint32_t max_pairs, const GridConfig &grid_config,
        uint32_t fallback_all_pairs_threshold
    ) : m_impl(std::make_unique<Impl>(render_system, max_pairs, grid_config, fallback_all_pairs_threshold)) {
    }

    SpatialHashBroadDetector::~SpatialHashBroadDetector() = default;

    bool SpatialHashBroadDetector::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    BroadDetectorOutputBuffers SpatialHashBroadDetector::GetOutputBuffers() const noexcept {
        return {
            .pair_buffer = *m_impl->gpu_collision_pairs,
            .pair_count_buffer = *m_impl->gpu_pair_count,
            .max_pairs = m_impl->max_pairs
        };
    }

    BroadDetectorOutputHandles SpatialHashBroadDetector::AddDetectPasses(
        RenderGraphBuilder &builder, PhysicsScene &physics_scene, const PhysicsSceneBufferHandles &handles
    ) {
        const auto gpu = physics_scene.GetGpuBuffers();
        if (gpu.shape_alive == nullptr || gpu.shape_world_position == nullptr || gpu.shape_slot_count == 0u) {
            return {};
        }

        const uint32_t shape_count = gpu.shape_slot_count;
        if (shape_count <= 1u) {
            // Zero the pair count and return.
            if (m_impl->gpu_pair_count) {
                auto *addr = reinterpret_cast<uint32_t *>(m_impl->gpu_pair_count->GetVMAddress());
                *addr = 0u;
            }
            return {};
        }

        m_impl->EnsureAllBuffers(shape_count);
        m_impl->EnsureInitialized();

        // Update shape slot count on GPU.
        {
            auto *addr = reinterpret_cast<uint32_t *>(m_impl->gpu_shape_slot_count->GetVMAddress());
            *addr = shape_count;
        }
        m_impl->UpdateGridConfigGpu();

        using AT = MemoryAccessTypeBufferBits;
        const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
        const MemoryAccessTypeBuffer RW{AT::ShaderRandomRead, AT::ShaderRandomWrite};
        const MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};

        // --- Use pre-imported scene buffer handles (no self-import) ---
        auto shape_alive_h = handles.shape_alive;
        auto shape_type_h = handles.shape_type;
        auto shape_feature_h = handles.shape_feature;
        auto shape_wpos_h = handles.shape_world_position;
        auto shape_wrot_h = handles.shape_world_rotation;

        // --- Resolve filter buffers (use dummy from detector if scene has none) ---
        const ComputeBuffer &filter_off_buf =
            gpu.shape_filter_offset ? *gpu.shape_filter_offset : *m_impl->gpu_dummy_uint;
        const ComputeBuffer &filter_cnt_buf =
            gpu.shape_filter_count ? *gpu.shape_filter_count : *m_impl->gpu_dummy_uint;
        const ComputeBuffer &filter_dat_buf =
            gpu.shape_filter_data ? *gpu.shape_filter_data : *m_impl->gpu_dummy_uint;

        // Filter handles: use pre-imported if the scene owns them, otherwise import dummy.
        auto filt_off_h = gpu.shape_filter_offset
            ? handles.shape_filter_offset
            : builder.ImportExternalResource(*m_impl->gpu_dummy_uint, {AT::None});
        auto filt_cnt_h = gpu.shape_filter_count
            ? handles.shape_filter_count
            : builder.ImportExternalResource(*m_impl->gpu_dummy_uint, {AT::None});
        auto filt_dat_h = gpu.shape_filter_data
            ? handles.shape_filter_data
            : builder.ImportExternalResource(*m_impl->gpu_dummy_uint, {AT::None});

        // --- Import owned buffers ---
        auto scount_h = builder.ImportExternalResource(*m_impl->gpu_shape_slot_count, {AT::None});
        auto aabb_min_h = builder.ImportExternalResource(*m_impl->gpu_aabb_min, {AT::None});
        auto aabb_max_h = builder.ImportExternalResource(*m_impl->gpu_aabb_max, {AT::None});
        auto global_h = builder.ImportExternalResource(*m_impl->gpu_global_flags, {AT::None});
        auto global_mode_h = builder.ImportExternalResource(*m_impl->gpu_global_mode, {AT::None});
        auto one_h = builder.ImportExternalResource(*m_impl->gpu_one, {AT::None});
        auto dummy_h = builder.ImportExternalResource(*m_impl->gpu_dummy_uint, {AT::None});
        auto scc_h = builder.ImportExternalResource(*m_impl->gpu_shape_cell_count, {AT::None});
        auto sco_h = builder.ImportExternalResource(*m_impl->gpu_shape_cell_offset, {AT::None});
        auto csp_h = builder.ImportExternalResource(*m_impl->gpu_cell_shape_pairs, {AT::None});
        auto total_h = builder.ImportExternalResource(*m_impl->gpu_total_assignments, {AT::None});
        auto hist_h = builder.ImportExternalResource(*m_impl->gpu_cell_histogram, {AT::None});
        auto coff_h = builder.ImportExternalResource(*m_impl->gpu_cell_offsets, {AT::None});
        auto cscr_h = builder.ImportExternalResource(*m_impl->gpu_cell_scratch, {AT::None});
        auto sorted_h = builder.ImportExternalResource(*m_impl->gpu_sorted_pairs, {AT::None});
        auto pairs_h = builder.ImportExternalResource(*m_impl->gpu_collision_pairs, {AT::None});
        auto pcnt_h = builder.ImportExternalResource(*m_impl->gpu_pair_count, {AT::None});
        auto gcfg_h = builder.ImportExternalResource(*m_impl->gpu_grid_config, {AT::None});
        auto scan_bs_h = builder.ImportExternalResource(*m_impl->gpu_scan_block_sums, {AT::None});

        // Helper: add a clear pass that zeros @p target (uses a per-pass binding).
        auto AddClearPass = [&](ComputeResourceBinding &binding, RGBufferHandle buf_handle,
                                 ComputeBuffer &target, ComputeBuffer &count_buf, const char *name) {
            auto &srb = binding.GetShaderResourceBinding();
            srb.BindBuffer("Target", target);
            srb.BindBuffer("ElemCount", count_buf);
            auto *stage = m_impl->memset_stage.get();
            auto *binding_ptr = &binding;
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName(name)
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(buf_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                    .SetPassFunction([stage, binding_ptr, &physics_scene](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(1, 1, 1);
                    })
                    .Get()
            );
        };

        // --- Determine if we use fallback ---
        bool use_fallback = (shape_count <= m_impl->fallback_threshold);

        // === Pass 1: Compute AABBs ===
        {
            auto &srb = m_impl->aabb_binding->GetShaderResourceBinding();
            srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
            srb.BindBuffer("ShapeType", *gpu.shape_type);
            srb.BindBuffer("ShapeFeature", *gpu.shape_feature);
            srb.BindBuffer("ShapeWorldPosition", *gpu.shape_world_position);
            srb.BindBuffer("ShapeWorldRotation", *gpu.shape_world_rotation);
            srb.BindBuffer("AabbMin", *m_impl->gpu_aabb_min);
            srb.BindBuffer("AabbMax", *m_impl->gpu_aabb_max);
            srb.BindBuffer("GlobalFlags", *m_impl->gpu_global_flags);
            srb.BindBuffer("ShapeSlotCount", *m_impl->gpu_shape_slot_count);
            srb.BindBuffer("GridConfig", *m_impl->gpu_grid_config);

            auto *stage = m_impl->aabb_stage.get();
            auto *binding = m_impl->aabb_binding;
            uint32_t wg = (shape_count + 63u) / 64u;
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("BH Compute AABBs")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(shape_alive_h, RR)
                    .UseBuffer(shape_type_h, RR)
                    .UseBuffer(shape_feature_h, RR)
                    .UseBuffer(shape_wpos_h, RR)
                    .UseBuffer(shape_wrot_h, RR)
                    .UseBuffer(aabb_min_h, WW)
                    .UseBuffer(aabb_max_h, WW)
                    .UseBuffer(global_h, WW)
                    .UseBuffer(scount_h, RR)
                    .UseBuffer(gcfg_h, RR)
                    .SetPassFunction([stage, binding, wg, &physics_scene](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        if (use_fallback) {
            // === Fallback: generate all-pairs directly ===
            // Set global mode to 0 (all-pairs).
            {
                auto *addr = reinterpret_cast<uint32_t *>(m_impl->gpu_global_mode->GetVMAddress());
                *addr = 0u;
            }
            auto &srb = m_impl->fallback_pairs_binding->GetShaderResourceBinding();
            srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
            srb.BindBuffer("ShapeSlotCount", *m_impl->gpu_shape_slot_count);
            srb.BindBuffer("CollisionPairs", *m_impl->gpu_collision_pairs);
            srb.BindBuffer("PairCount", *m_impl->gpu_pair_count);
            srb.BindBuffer("GlobalFlags", *m_impl->gpu_global_flags);
            srb.BindBuffer("GlobalMode", *m_impl->gpu_global_mode);
            srb.BindBuffer("ShapeFilterOffset", filter_off_buf);
            srb.BindBuffer("ShapeFilterCount", filter_cnt_buf);
            srb.BindBuffer("ShapeFilterData", filter_dat_buf);

            uint32_t total_pairs = (shape_count * (shape_count - 1u)) / 2u;
            uint32_t wg = (total_pairs + 63u) / 64u;
            auto *stage = m_impl->fallback_pairs_stage.get();
            auto *binding = m_impl->fallback_pairs_binding;

            // Clear pair count on GPU before accumulation.
            {
                ComputeResourceBinding &cbind = m_impl->memset_stage->AllocateResourceBinding();
                AddClearPass(cbind, pcnt_h, *m_impl->gpu_pair_count, *m_impl->gpu_one, "BH Clear PairCount");
            }

            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("BH Fallback AllPairs")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(shape_alive_h, RR)
                    .UseBuffer(scount_h, RR)
                    .UseBuffer(pairs_h, WW)
                    .UseBuffer(pcnt_h, WW)
                    .UseBuffer(global_h, RR)
                    .UseBuffer(global_mode_h, RR)
                    .UseBuffer(filt_off_h, RR)
                    .UseBuffer(filt_cnt_h, RR)
                    .UseBuffer(filt_dat_h, RR)
                    .SetPassFunction([stage, binding, wg, &physics_scene](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
            return {pairs_h, pcnt_h};
        }

        // Clear total_assignments on GPU before count_cells accumulation.
        {
            ComputeResourceBinding &cbind = m_impl->memset_stage->AllocateResourceBinding();
            AddClearPass(cbind, total_h, *m_impl->gpu_total_assignments, *m_impl->gpu_one, "BH Clear TotalAssign");
        }

        // === Pass 2: Count cells per shape ===
        {
            auto &srb = m_impl->count_cells_binding->GetShaderResourceBinding();
            srb.BindBuffer("AabbMin", *m_impl->gpu_aabb_min);
            srb.BindBuffer("AabbMax", *m_impl->gpu_aabb_max);
            srb.BindBuffer("GlobalFlags", *m_impl->gpu_global_flags);
            srb.BindBuffer("GridConfig", *m_impl->gpu_grid_config);
            srb.BindBuffer("ShapeSlotCount", *m_impl->gpu_shape_slot_count);
            srb.BindBuffer("ShapeCellCount", *m_impl->gpu_shape_cell_count);
            srb.BindBuffer("TotalAssignments", *m_impl->gpu_total_assignments);

            auto *stage = m_impl->count_cells_stage.get();
            auto *binding = m_impl->count_cells_binding;
            uint32_t wg = (shape_count + 63u) / 64u;
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("BH Count Cells")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(aabb_min_h, RR)
                    .UseBuffer(aabb_max_h, RR)
                    .UseBuffer(global_h, RR)
                    .UseBuffer(gcfg_h, RR)
                    .UseBuffer(scount_h, RR)
                    .UseBuffer(scc_h, WW)
                    .UseBuffer(total_h, WW)
                    .SetPassFunction([stage, binding, wg, &physics_scene](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        // === Prefix sum: shape_cell_count → shape_cell_offset ===
        // m_impl->DispatchParallelScan(
        //     builder, sco_h, *m_impl->gpu_shape_cell_count, *m_impl->gpu_scan_block_sums, shape_count
        // );
        {
            if (shape_count > 512u) {
                throw std::runtime_error("BroadPhase: shape_cell_count scan > 512 not implemented yet");
            }
            // Write scan params {mode, block_offset} to host-visible buffer.
            {
                auto *addr = reinterpret_cast<uint32_t *>(m_impl->gpu_scan_params->GetVMAddress());
                addr[0] = 1u; // mode
                addr[1] = 0u; // block offset
            }

            auto *stage = m_impl->scan_stage.get();
            auto *binding = &stage->AllocateResourceBinding();
            auto &srb = binding->GetShaderResourceBinding();
            // In-place: input and output are the same buffer.
            srb.BindBuffer("InputData", *m_impl->gpu_shape_cell_count);
            srb.BindBuffer("OutputData", *m_impl->gpu_cell_offsets);
            srb.BindBuffer("ElemCount", *m_impl->gpu_shape_slot_count);
            srb.BindBuffer("ScanParams", *m_impl->gpu_scan_params);
            srb.BindBuffer("BlockSums", *m_impl->gpu_pair_count);

            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("BroadPhase ParallelScan")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(scc_h, RR)
                    .UseBuffer(sco_h, WW)
                    .UseBuffer(scount_h, RR)
                    .UseBuffer(pcnt_h, WW)
                    .SetPassFunction([stage, binding](CommandBuffer &cb, const RenderGraph &) -> void {
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(1, 1, 1);
                    })
                    .Get()
            );
            // Note: shape_cell_count is overwritten with shape_cell_offset (in-place scan).
            // The offset buffer is the same as the count buffer after scanning.
        }

        // === Pass 3: Fill cell_shape_pairs ===
        {
            auto &srb = m_impl->fill_cells_binding->GetShaderResourceBinding();
            srb.BindBuffer("AabbMin", *m_impl->gpu_aabb_min);
            srb.BindBuffer("AabbMax", *m_impl->gpu_aabb_max);
            srb.BindBuffer("GlobalFlags", *m_impl->gpu_global_flags);
            srb.BindBuffer("GridConfig", *m_impl->gpu_grid_config);
            srb.BindBuffer("ShapeSlotCount", *m_impl->gpu_shape_slot_count);
            srb.BindBuffer("ShapeCellOffset", *m_impl->gpu_cell_offsets);
            srb.BindBuffer("CellShapePairs", *m_impl->gpu_cell_shape_pairs);

            auto *stage = m_impl->fill_cells_stage.get();
            auto *binding = m_impl->fill_cells_binding;
            uint32_t wg = (shape_count + 63u) / 64u;
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("BH Fill Cells")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(aabb_min_h, RR)
                    .UseBuffer(aabb_max_h, RR)
                    .UseBuffer(global_h, RR)
                    .UseBuffer(gcfg_h, RR)
                    .UseBuffer(scount_h, RR)
                    .UseBuffer(sco_h, RR)
                    .UseBuffer(csp_h, WW)
                    .SetPassFunction([stage, binding, wg, &physics_scene](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        // === Clear cell histogram before atomic accumulation ===
        {
            ComputeResourceBinding &bind = m_impl->memset_stage->AllocateResourceBinding();
            auto &srb = bind.GetShaderResourceBinding();
            srb.BindBuffer("Target", *m_impl->gpu_cell_histogram);
            srb.BindBuffer("ElemCount", *m_impl->gpu_grid_cells_p1);

            auto *stage = m_impl->memset_stage.get();
            auto *binding_ptr = &bind;
            uint32_t wg = (m_impl->grid_total_cells + 1u + 63u) / 64u;
            // Import gpu_grid_cells_p1 handle for the render graph.
            auto cells_p1_h = builder.ImportExternalResource(*m_impl->gpu_grid_cells_p1, {AT::None});
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("BH Clear Histogram")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(hist_h, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                    .UseBuffer(cells_p1_h, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .SetPassFunction([stage, binding_ptr, wg, &physics_scene](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        // === Pass 4: Histogram ===
        {
            auto &srb = m_impl->histogram_binding->GetShaderResourceBinding();
            srb.BindBuffer("CellShapePairs", *m_impl->gpu_cell_shape_pairs);
            srb.BindBuffer("CellHistogram", *m_impl->gpu_cell_histogram);
            srb.BindBuffer("TotalAssignments", *m_impl->gpu_total_assignments);

            auto *stage = m_impl->histogram_stage.get();
            auto *binding = m_impl->histogram_binding;
            uint32_t max_assignments = shape_count * std::max(1u, m_impl->grid_config.max_cells_per_shape);
            uint32_t wg = (max_assignments + 63u) / 64u;
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("BH Histogram")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(csp_h, RR)
                    .UseBuffer(hist_h, WW)
                    .UseBuffer(total_h, RR)
                    .SetPassFunction([stage, binding, wg, &physics_scene](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        // === Prefix sum: cell_histogram → cell_offsets ===
        // m_impl->DispatchParallelScan(
        //     builder, coff_h, *m_impl->gpu_cell_histogram, *m_impl->gpu_scan_block_sums,
        //     m_impl->grid_total_cells + 1u
        // );
        // cell_histogram now holds cell_offsets (exclusive scan).

        // === Copy cell_offsets → cell_scratch (initialize atomic counters) ===
        {
            ComputeResourceBinding &bind = m_impl->copy_stage->AllocateResourceBinding();
            auto &srb = bind.GetShaderResourceBinding();
            srb.BindBuffer("SrcBuffer", *m_impl->gpu_cell_histogram);
            srb.BindBuffer("DstBuffer", *m_impl->gpu_cell_scratch);
            srb.BindBuffer("ElemCount", *m_impl->gpu_grid_cells_p1);

            auto *stage = m_impl->copy_stage.get();
            auto *binding_ptr = &bind;
            uint32_t wg = (m_impl->grid_total_cells + 1u + 63u) / 64u;
            auto cells_p1_h = builder.ImportExternalResource(*m_impl->gpu_grid_cells_p1, {AT::None});
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("BH Copy Offsets → Scratch")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(coff_h, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .UseBuffer(cscr_h, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                    .UseBuffer(cells_p1_h, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .SetPassFunction([stage, binding_ptr, wg, &physics_scene](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        // === Pass 5: Scatter sort ===
        {
            auto &srb = m_impl->scatter_sort_binding->GetShaderResourceBinding();
            srb.BindBuffer("CellShapePairs", *m_impl->gpu_cell_shape_pairs);
            srb.BindBuffer("SortedPairs", *m_impl->gpu_sorted_pairs);
            srb.BindBuffer("CellOffsets", *m_impl->gpu_cell_histogram); // holds offsets
            srb.BindBuffer("CellScratch", *m_impl->gpu_cell_scratch);
            srb.BindBuffer("TotalAssignments", *m_impl->gpu_total_assignments);

            auto *stage = m_impl->scatter_sort_stage.get();
            auto *binding = m_impl->scatter_sort_binding;
            uint32_t max_assignments = shape_count * std::max(1u, m_impl->grid_config.max_cells_per_shape);
            uint32_t wg = (max_assignments + 63u) / 64u;
            builder.AddPass(
                RenderGraphPassBuilder{m_impl->render_system}
                    .SetName("BH Scatter Sort")
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(csp_h, RR)
                    .UseBuffer(sorted_h, WW)
                    .UseBuffer(coff_h, RR)
                    .UseBuffer(cscr_h, WW)
                    .UseBuffer(total_h, RR)
                    .SetPassFunction([stage, binding, wg, &physics_scene](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!physics_scene.IsSimulationEnabled()) return;
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding);
                        cb.DispatchCompute(wg, 1, 1);
                    })
                    .Get()
            );
        }

        // === Pass 6: Generate collision pairs ===
        // {
        //     auto &srb = m_impl->generate_pairs_binding->GetShaderResourceBinding();
        //     srb.BindBuffer("SortedPairs", *m_impl->gpu_sorted_pairs);
        //     srb.BindBuffer("CellOffsets", *m_impl->gpu_cell_histogram); // holds offsets
        //     srb.BindBuffer("GlobalFlags", *m_impl->gpu_global_flags);
        //     srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
        //     srb.BindBuffer("ShapeSlotCount", *m_impl->gpu_shape_slot_count);
        //     srb.BindBuffer("CollisionPairs", *m_impl->gpu_collision_pairs);
        //     srb.BindBuffer("PairCount", *m_impl->gpu_pair_count);
        //     srb.BindBuffer("GridConfig", *m_impl->gpu_grid_config);
        //     srb.BindBuffer("TotalAssignments", *m_impl->gpu_total_assignments);
        //     srb.BindBuffer("ShapeFilterOffset", filter_off_buf);
        //     srb.BindBuffer("ShapeFilterCount", filter_cnt_buf);
        //     srb.BindBuffer("ShapeFilterData", filter_dat_buf);

        //     // Clear pair count on GPU before accumulation.
        //     {
        //         ComputeResourceBinding &cbind = m_impl->memset_stage->AllocateResourceBinding();
        //         AddClearPass(cbind, pcnt_h, *m_impl->gpu_pair_count, *m_impl->gpu_one, "BH Clear PairCount");
        //     }

        //     auto *stage = m_impl->generate_pairs_stage.get();
        //     auto *binding = m_impl->generate_pairs_binding;
        //     // Dispatch one workgroup per grid cell.
        //     uint32_t wg = (m_impl->grid_total_cells + 63u) / 64u;
        //     builder.AddPass(
        //         RenderGraphPassBuilder{m_impl->render_system}
        //             .SetName("BH Generate Pairs")
        //             .SetAffinity(RenderGraphPassAffinity::Compute)
        //             .UseBuffer(sorted_h, RR)
        //             .UseBuffer(coff_h, RR)
        //             .UseBuffer(global_h, RR)
        //             .UseBuffer(shape_alive_h, RR)
        //             .UseBuffer(scount_h, RR)
        //             .UseBuffer(pairs_h, WW)
        //             .UseBuffer(pcnt_h, WW)
        //             .UseBuffer(gcfg_h, RR)
        //             .UseBuffer(total_h, RR)
        //             .UseBuffer(filt_off_h, RR)
        //             .UseBuffer(filt_cnt_h, RR)
        //             .UseBuffer(filt_dat_h, RR)
        //             .SetPassFunction([stage, binding, wg, &physics_scene](CommandBuffer &cb, const RenderGraph &) -> void {
        //                 if (!physics_scene.IsSimulationEnabled()) return;
        //                 cb.BindComputeStage(*stage);
        //                 cb.BindComputeResource(*binding);
        //                 cb.DispatchCompute(wg, 1, 1);
        //             })
        //             .Get()
        //     );
        // }

        return {pairs_h, pcnt_h};
    }
} // namespace Engine
