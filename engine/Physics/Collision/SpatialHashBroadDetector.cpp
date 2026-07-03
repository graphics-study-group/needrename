#include "SpatialHashBroadDetector.h"

#include <Physics/gpu_algorithm/CompactUnique.h>
#include <Physics/gpu_algorithm/ParallelScan.h>
#include <Physics/gpu_algorithm/RadixSort.h>

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
#include <Render/Pipeline/RenderGraph/RenderGraph.h>
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
        glm::vec4 world_min;  // xyz = bounds min, w = cell_size
        glm::ivec4 grid_dims; // xyz = grid dimensions, w = max_cells_per_shape
        uint32_t total_cells; // grid_dims.x * grid_dims.y * grid_dims.z
        uint32_t _pad[3];
    };
    static_assert(sizeof(GridConfigGpu) == 48, "GridConfigGpu must match shader UBO layout");

    struct SpatialHashBroadDetector::Impl {
        RenderSystem &render_system;
        GridConfig grid_config{};
        uint32_t fallback_threshold = 8;
        uint32_t max_global_shape_count = 100;

        // Cached from Configure.
        PhysicsScene *cached_scene = nullptr;
        uint32_t cached_shape_count = 0;
        uint32_t max_assignment_pairs = 0;

        bool shaders_loaded = false;

        // Grid dimensions (computed from grid_config in Configure).
        uint32_t grid_total_cells = 0;
        glm::ivec3 grid_dims{};

        // ---- Compute stages (one per shader) ----
        std::unique_ptr<ComputeStage> aabb_stage{};
        ComputeResourceBinding *aabb_binding = nullptr;
        std::vector<uint32_t> aabb_spirv{};

        std::unique_ptr<ComputeStage> count_cells_stage{};
        ComputeResourceBinding *count_cells_binding = nullptr;
        std::vector<uint32_t> count_cells_spirv{};

        std::unique_ptr<ComputeStage> fill_cells_stage{};
        ComputeResourceBinding *fill_cells_binding = nullptr;
        std::vector<uint32_t> fill_cells_spirv{};

        std::unique_ptr<ComputeStage> histogram_stage{};
        ComputeResourceBinding *histogram_binding = nullptr;
        std::vector<uint32_t> histogram_spirv{};

        std::unique_ptr<ComputeStage> scatter_sort_stage{};
        ComputeResourceBinding *scatter_sort_binding = nullptr;
        std::vector<uint32_t> scatter_sort_spirv{};

        std::unique_ptr<ComputeStage> generate_pairs_stage{};
        ComputeResourceBinding *generate_pairs_binding = nullptr;
        std::vector<uint32_t> generate_pairs_spirv{};

        std::unique_ptr<ComputeStage> fallback_pairs_stage{};
        ComputeResourceBinding *fallback_pairs_binding = nullptr;
        std::vector<uint32_t> fallback_pairs_spirv{};

        std::unique_ptr<ComputeStage> global_pairs_stage{};
        ComputeResourceBinding *global_pairs_binding = nullptr;
        std::vector<uint32_t> global_pairs_spirv{};

        std::unique_ptr<ComputeStage> memset_stage{};
        std::vector<uint32_t> memset_spirv{};

        std::unique_ptr<ComputeStage> copy_stage{};
        std::vector<uint32_t> copy_spirv{};

        // ---- Owned GPU buffers ----
        std::unique_ptr<ComputeBuffer> gpu_aabb_min{};
        std::unique_ptr<ComputeBuffer> gpu_aabb_max{};
        std::unique_ptr<ComputeBuffer> gpu_shape_cell_count{};
        std::unique_ptr<ComputeBuffer> gpu_shape_cell_offset{};
        std::unique_ptr<ComputeBuffer> gpu_cell_shape_pairs{};
        std::unique_ptr<ComputeBuffer> gpu_total_assignments{};
        std::unique_ptr<ComputeBuffer> gpu_cell_histogram{};
        std::unique_ptr<ComputeBuffer> gpu_cell_offsets{};
        std::unique_ptr<ComputeBuffer> gpu_cell_scratch{};
        std::unique_ptr<ComputeBuffer> gpu_sorted_pairs{};
        std::unique_ptr<ComputeBuffer> gpu_global_flags{};
        std::unique_ptr<ComputeBuffer> gpu_global_list{};
        std::unique_ptr<ComputeBuffer> gpu_global_count{};
        std::unique_ptr<ComputeBuffer> gpu_collision_pairs{};
        std::unique_ptr<ComputeBuffer> gpu_pair_count{};
        std::unique_ptr<ComputeBuffer> gpu_grid_config{};
        std::unique_ptr<ComputeBuffer> gpu_shape_slot_count{};
        std::unique_ptr<ComputeBuffer> gpu_one{};
        std::unique_ptr<ComputeBuffer> gpu_grid_cells_p1{};

        // Dedup buffers.
        std::unique_ptr<ComputeBuffer> gpu_pairs_temp{};     // ping-pong for RadixSort
        std::unique_ptr<ComputeBuffer> gpu_radix_scratch{};  // 256-uint histogram
        std::unique_ptr<ComputeBuffer> gpu_unique_flags{};   // original 0/1 flags
        std::unique_ptr<ComputeBuffer> gpu_unique_offsets{}; // prefix-sum offsets
        std::unique_ptr<ComputeBuffer> gpu_unique_count{};   // output unique count

        // Parallel prefix-sum executor (reusable across scan sites).
        std::unique_ptr<ParallelScan> scan{};

        // Dedup algorithm instances.
        std::unique_ptr<RadixSort> radix_sort{};
        std::unique_ptr<CompactUnique> compact_unique{};

        // Block-sums scratch buffer for ParallelScan.
        std::unique_ptr<ComputeBuffer> gpu_scan_scratch{};

        // ---- Self-owned RenderGraph ----
        std::unique_ptr<RenderGraph> render_graph{};
        uint32_t cached_rg_shape_count = 0;  // For RG rebuild detection.
        bool cached_rg_use_fallback = false; // RG structure may change if threshold crossed.

        explicit Impl(RenderSystem &rs) : render_system(rs) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        // -------------------------------------------------------------------
        // Buffer helpers
        // -------------------------------------------------------------------

        void EnsureBuffer(
            std::unique_ptr<ComputeBuffer> &buf, size_t bytes, const char *name, bool host_visible = false
        ) {
            const auto &alloc = render_system.GetAllocatorState();
            if (!buf || buf->GetSize() != bytes) {
                buf = ComputeBuffer::CreateUnique(alloc, bytes, host_visible, false, false, false, name);
            }
        }

        void EnsureAllBuffers(uint32_t shape_count) {
            const auto &alloc = render_system.GetAllocatorState();

            EnsureBuffer(gpu_shape_slot_count, sizeof(uint32_t), "BH ShapeSlotCount", true);

            const size_t shape_vec4 = static_cast<size_t>(shape_count) * sizeof(glm::vec4);
            const size_t shape_uint = static_cast<size_t>(shape_count) * sizeof(uint32_t);

            EnsureBuffer(gpu_aabb_min, shape_vec4, "BH AabbMin");
            EnsureBuffer(gpu_aabb_max, shape_vec4, "BH AabbMax");
            EnsureBuffer(gpu_shape_cell_count, shape_uint, "BH ShapeCellCnt");
            EnsureBuffer(gpu_shape_cell_offset, shape_uint, "BH ShapeCellOff");
            EnsureBuffer(gpu_global_flags, shape_uint, "BH GlobalFlags");

            EnsureBuffer(gpu_total_assignments, sizeof(uint32_t), "BH TotalAssign", true);

            EnsureBuffer(
                gpu_cell_shape_pairs, static_cast<size_t>(max_assignment_pairs) * sizeof(glm::uvec2), "BH CellShapePairs"
            );
            EnsureBuffer(gpu_sorted_pairs, static_cast<size_t>(max_assignment_pairs) * sizeof(glm::uvec2), "BH SortedPairs");

            size_t cell_uint1 = static_cast<size_t>(grid_total_cells + 1u) * sizeof(uint32_t);
            EnsureBuffer(gpu_cell_histogram, cell_uint1, "BH CellHist");
            EnsureBuffer(gpu_cell_offsets, cell_uint1, "BH CellOffsets");
            EnsureBuffer(gpu_cell_scratch, cell_uint1, "BH CellScratch");

            EnsureBuffer(gpu_collision_pairs, static_cast<size_t>(max_assignment_pairs) * sizeof(glm::uvec2), "BH CollisionPairs");
            EnsureBuffer(gpu_pair_count, sizeof(uint32_t), "BH PairCount", true);

            {
                const size_t list_bytes = static_cast<size_t>(std::max(1u, shape_count)) * sizeof(uint32_t);
                if (!gpu_global_list || gpu_global_list->GetSize() < list_bytes) {
                    gpu_global_list =
                        ComputeBuffer::CreateUnique(alloc, list_bytes, false, false, false, false, "BH GlobalList");
                }
            }
            if (!gpu_global_count || gpu_global_count->GetSize() < sizeof(uint32_t)) {
                gpu_global_count =
                    ComputeBuffer::CreateUnique(alloc, sizeof(uint32_t), true, false, false, false, "BH GlobalCount");
            }

            if (!gpu_one || gpu_one->GetSize() < sizeof(uint32_t)) {
                gpu_one = ComputeBuffer::CreateUnique(alloc, sizeof(uint32_t), true, false, false, false, "BH One");
                auto *addr = reinterpret_cast<uint32_t *>(gpu_one->GetVMAddress());
                *addr = 1u;
            }

            // Dedup buffers.
            {
                const size_t temp_bytes = static_cast<size_t>(max_assignment_pairs) * sizeof(glm::uvec2);
                EnsureBuffer(gpu_pairs_temp, temp_bytes, "BH PairsTemp");
            }
            EnsureBuffer(gpu_radix_scratch, RadixSort::GetRequiredScratchBytes(), "BH RadixScratch");
            {
                const size_t flags_bytes = CompactUnique::GetRequiredFlagBytes(max_assignment_pairs);
                EnsureBuffer(gpu_unique_flags, flags_bytes, "BH UniqueFlags");
                EnsureBuffer(gpu_unique_offsets, flags_bytes, "BH UniqueOffsets");
            }
            if (!gpu_unique_count || gpu_unique_count->GetSize() < sizeof(uint32_t)) {
                gpu_unique_count =
                    ComputeBuffer::CreateUnique(alloc, sizeof(uint32_t), true, false, false, false, "BH UniqueCount");
            }

            if (!gpu_grid_cells_p1 || gpu_grid_cells_p1->GetSize() < sizeof(uint32_t)) {
                gpu_grid_cells_p1 =
                    ComputeBuffer::CreateUnique(alloc, sizeof(uint32_t), true, false, false, false, "BH GridCellsP1");
                auto *addr = reinterpret_cast<uint32_t *>(gpu_grid_cells_p1->GetVMAddress());
                *addr = grid_total_cells + 1u;
            }

            EnsureBuffer(gpu_grid_config, sizeof(GridConfigGpu), "BH GridConfig", true);

            {
                size_t scratch_bytes = ParallelScan::GetRequiredBlockSumsBytes(max_assignment_pairs);
                EnsureBuffer(gpu_scan_scratch, scratch_bytes, "BH ScanScratch");
            }
        }

        // -------------------------------------------------------------------
        // Lazy shader loading
        // -------------------------------------------------------------------

        void EnsureShadersLoaded() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            const char *base = "collision/SpatialHashBroadDetector/";
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

            fallback_pairs_spirv =
                LoadPhysicsSpirvBytes((std::string(base) + "generate_all_pairs_fallback.comp.spv").c_str());
            fallback_pairs_stage = std::make_unique<ComputeStage>(render_system);
            fallback_pairs_stage->Instantiate(fallback_pairs_spirv, "BH FallbackPairs");
            fallback_pairs_binding = &fallback_pairs_stage->AllocateResourceBinding();

            global_pairs_spirv = LoadPhysicsSpirvBytes((std::string(base) + "generate_global_pairs.comp.spv").c_str());
            global_pairs_stage = std::make_unique<ComputeStage>(render_system);
            global_pairs_stage->Instantiate(global_pairs_spirv, "BH GlobalPairs");
            global_pairs_binding = &global_pairs_stage->AllocateResourceBinding();

            const char *memset_path = "collision/SpatialHashBroadDetector/memset_uint.comp.spv";
            memset_spirv = LoadPhysicsSpirvBytes(memset_path);
            memset_stage = std::make_unique<ComputeStage>(render_system);
            memset_stage->Instantiate(memset_spirv, "BH MemsetUint");

            const char *copy_path = "collision/SpatialHashBroadDetector/copy_uint.comp.spv";
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

        // -------------------------------------------------------------------
        // RG build
        // -------------------------------------------------------------------

        void AddClearPass(
            RenderGraphBuilder &builder,
            ComputeResourceBinding &binding,
            RGBufferHandle target_handle,
            RGBufferHandle count_handle,
            ComputeBuffer &target,
            ComputeBuffer &count_buf,
            const char *name
        ) {
            auto &srb = binding.GetShaderResourceBinding();
            srb.BindBuffer("Target", target);
            srb.BindBuffer("ElemCount", count_buf);
            auto *stage = memset_stage.get();
            auto *binding_ptr = &binding;
            auto *scene_ptr = cached_scene;
            builder.AddPass(
                RenderGraphPassBuilder{render_system}
                    .SetName(name)
                    .SetAffinity(RenderGraphPassAffinity::Compute)
                    .UseBuffer(target_handle, {MemoryAccessTypeBufferBits::ShaderRandomWrite})
                    .UseBuffer(count_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                    .SetPassFunction([stage, binding_ptr, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                        if (!scene_ptr->IsSimulationEnabled()) return;
                        cb.BindComputeStage(*stage);
                        cb.BindComputeResource(*binding_ptr);
                        cb.DispatchCompute(1, 1, 1);
                    })
                    .Get()
            );
        }

        std::unique_ptr<RenderGraph> BuildRenderGraph() {
            assert(cached_scene && "Configure must be called before Detect");
            const auto gpu = cached_scene->GetGpuBuffers();
            const uint32_t shape_count = cached_shape_count;

            RenderGraphBuilder builder{render_system};

            using AT = MemoryAccessTypeBufferBits;
            const MemoryAccessTypeBuffer RR{AT::ShaderRandomRead};
            const MemoryAccessTypeBuffer RW{AT::ShaderRandomRead, AT::ShaderRandomWrite};
            const MemoryAccessTypeBuffer WW{AT::ShaderRandomWrite};

            // prev_access for scene buffers:
            // The PreCollisionRG (solver) runs before us and writes shape_world_position/rotation (RW).
            // It also reads shape_alive/type/feature (RR).  We read all of them.
            // Conservative: use RR for read-only scene buffers.  The solver's writes to shape
            // world poses are already flushed by the RG barrier at end of PreCollisionRG.

            // --- Import scene buffers ---
            auto shape_alive_h = builder.ImportExternalResource(*gpu.shape_alive, RR);
            auto shape_type_h = builder.ImportExternalResource(*gpu.shape_type, RR);
            auto shape_feature_h = builder.ImportExternalResource(*gpu.shape_feature, RR);
            auto shape_wpos_h = builder.ImportExternalResource(*gpu.shape_world_position, RR);
            auto shape_wrot_h = builder.ImportExternalResource(*gpu.shape_world_rotation, RR);

            // --- Resolve filter buffers ---
            auto filt_off_h = builder.ImportExternalResource(*gpu.shape_filter_offset, RR);
            auto filt_cnt_h = builder.ImportExternalResource(*gpu.shape_filter_count, RR);
            auto filt_dat_h = builder.ImportExternalResource(*gpu.shape_filter_data, RR);

            // --- Import owned buffers ---
            auto scount_h = builder.ImportExternalResource(*gpu_shape_slot_count, RR);
            auto aabb_min_h = builder.ImportExternalResource(*gpu_aabb_min, RW);
            auto aabb_max_h = builder.ImportExternalResource(*gpu_aabb_max, RW);
            auto global_h = builder.ImportExternalResource(*gpu_global_flags, RW);
            auto global_list_h = builder.ImportExternalResource(*gpu_global_list, RW);
            auto global_count_h = builder.ImportExternalResource(*gpu_global_count, RW);
            auto one_h = builder.ImportExternalResource(*gpu_one, RR);
            auto scc_h = builder.ImportExternalResource(*gpu_shape_cell_count, RW);
            auto sco_h = builder.ImportExternalResource(*gpu_shape_cell_offset, RW);
            auto csp_h = builder.ImportExternalResource(*gpu_cell_shape_pairs, RW);
            auto total_h = builder.ImportExternalResource(*gpu_total_assignments, RW);
            auto hist_h = builder.ImportExternalResource(*gpu_cell_histogram, RW);
            auto coff_h = builder.ImportExternalResource(*gpu_cell_offsets, RW);
            auto cscr_h = builder.ImportExternalResource(*gpu_cell_scratch, RW);
            auto sorted_h = builder.ImportExternalResource(*gpu_sorted_pairs, RW);
            auto pairs_h = builder.ImportExternalResource(*gpu_collision_pairs, RW);
            auto pcnt_h = builder.ImportExternalResource(*gpu_pair_count, RW);
            auto gcfg_h = builder.ImportExternalResource(*gpu_grid_config, RW);
            auto cells_p1_h = builder.ImportExternalResource(*gpu_grid_cells_p1, RW);
            auto scan_scratch_h = builder.ImportExternalResource(*gpu_scan_scratch, RW);

            // --- Import dedup buffers ---
            auto temp_pairs_h = builder.ImportExternalResource(*gpu_pairs_temp, RW);
            auto radix_scratch_h = builder.ImportExternalResource(*gpu_radix_scratch, RW);
            auto unique_flags_h = builder.ImportExternalResource(*gpu_unique_flags, RW);
            auto unique_offsets_h = builder.ImportExternalResource(*gpu_unique_offsets, RW);
            auto unique_count_h = builder.ImportExternalResource(*gpu_unique_count, RW);

            if (scan) scan->ResetGraph();
            if (radix_sort) radix_sort->ResetGraph();

            // --- Determine if we use fallback ---
            bool use_fallback = (shape_count <= fallback_threshold);

            // Clear global_count on GPU before AABB pass.
            {
                ComputeResourceBinding &cbind = memset_stage->AllocateResourceBinding();
                AddClearPass(
                    builder, cbind, global_count_h, one_h, *gpu_global_count, *gpu_one, "BH Clear GlobalCount"
                );
            }

            // === Pass 1: Compute AABBs ===
            {
                auto &srb = aabb_binding->GetShaderResourceBinding();
                srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
                srb.BindBuffer("ShapeType", *gpu.shape_type);
                srb.BindBuffer("ShapeFeature", *gpu.shape_feature);
                srb.BindBuffer("ShapeWorldPosition", *gpu.shape_world_position);
                srb.BindBuffer("ShapeWorldRotation", *gpu.shape_world_rotation);
                srb.BindBuffer("AabbMin", *gpu_aabb_min);
                srb.BindBuffer("AabbMax", *gpu_aabb_max);
                srb.BindBuffer("GlobalFlags", *gpu_global_flags);
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("GridConfig", *gpu_grid_config);
                srb.BindBuffer("GlobalList", *gpu_global_list);
                srb.BindBuffer("GlobalCount", *gpu_global_count);

                auto *stage = aabb_stage.get();
                auto *binding = aabb_binding;
                uint32_t wg = (shape_count + 63u) / 64u;
                auto *scene_ptr = cached_scene;
                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
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
                        .UseBuffer(global_list_h, WW)
                        .UseBuffer(global_count_h, RW)
                        .UseBuffer(scount_h, RR)
                        .UseBuffer(gcfg_h, RR)
                        .SetPassFunction(
                            [stage, binding, wg, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!scene_ptr->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding);
                                cb.DispatchCompute(wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            if (use_fallback) {
                // === Fallback: generate all-pairs directly ===
                auto &srb = fallback_pairs_binding->GetShaderResourceBinding();
                srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("CollisionPairs", *gpu_collision_pairs);
                srb.BindBuffer("PairCount", *gpu_pair_count);
                srb.BindBuffer("ShapeFilterOffset", *gpu.shape_filter_offset);
                srb.BindBuffer("ShapeFilterCount", *gpu.shape_filter_count);
                srb.BindBuffer("ShapeFilterData", *gpu.shape_filter_data);
                srb.BindBuffer("AabbMin", *gpu_aabb_min);
                srb.BindBuffer("AabbMax", *gpu_aabb_max);

                uint32_t total_pairs = (shape_count * (shape_count - 1u)) / 2u;
                uint32_t wg = (total_pairs + 63u) / 64u;
                auto *stage = fallback_pairs_stage.get();
                auto *binding = fallback_pairs_binding;
                auto *scene_ptr = cached_scene;

                // Clear pair count before accumulation.
                {
                    ComputeResourceBinding &cbind = memset_stage->AllocateResourceBinding();
                    AddClearPass(builder, cbind, pcnt_h, one_h, *gpu_pair_count, *gpu_one, "BH Clear PairCount");
                }

                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("BH Fallback AllPairs")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(shape_alive_h, RR)
                        .UseBuffer(scount_h, RR)
                        .UseBuffer(pairs_h, WW)
                        .UseBuffer(pcnt_h, WW)
                        .UseBuffer(filt_off_h, RR)
                        .UseBuffer(filt_cnt_h, RR)
                        .UseBuffer(filt_dat_h, RR)
                        .UseBuffer(aabb_min_h, RR)
                        .UseBuffer(aabb_max_h, RR)
                        .SetPassFunction(
                            [stage, binding, wg, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!scene_ptr->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding);
                                cb.DispatchCompute(wg, 1, 1);
                            }
                        )
                        .Get()
                );
                return builder.BuildRenderGraph();
            }

            // === Spatial hash path ===

            // Clear total_assignments before count_cells.
            {
                ComputeResourceBinding &cbind = memset_stage->AllocateResourceBinding();
                AddClearPass(builder, cbind, total_h, one_h, *gpu_total_assignments, *gpu_one, "BH Clear TotalAssign");
            }

            // === Pass 2: Count cells per shape ===
            {
                auto &srb = count_cells_binding->GetShaderResourceBinding();
                srb.BindBuffer("AabbMin", *gpu_aabb_min);
                srb.BindBuffer("AabbMax", *gpu_aabb_max);
                srb.BindBuffer("GlobalFlags", *gpu_global_flags);
                srb.BindBuffer("GridConfig", *gpu_grid_config);
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("ShapeCellCount", *gpu_shape_cell_count);
                srb.BindBuffer("TotalAssignments", *gpu_total_assignments);

                auto *stage = count_cells_stage.get();
                auto *binding = count_cells_binding;
                uint32_t wg = (shape_count + 63u) / 64u;
                auto *scene_ptr = cached_scene;
                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("BH Count Cells")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(aabb_min_h, RR)
                        .UseBuffer(aabb_max_h, RR)
                        .UseBuffer(global_h, RR)
                        .UseBuffer(gcfg_h, RR)
                        .UseBuffer(scount_h, RR)
                        .UseBuffer(scc_h, WW)
                        .UseBuffer(total_h, WW)
                        .SetPassFunction(
                            [stage, binding, wg, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!scene_ptr->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding);
                                cb.DispatchCompute(wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // === Prefix sum: shape_cell_count → shape_cell_offset ===
            {
                if (!scan || scan->GetMaxElemCount() < max_assignment_pairs) {
                    scan = std::make_unique<ParallelScan>(render_system, max_assignment_pairs);
                }
                scan->AddPasses(
                    builder,
                    scc_h,
                    sco_h,
                    *gpu_shape_cell_count,
                    *gpu_shape_cell_offset,
                    scan_scratch_h,
                    *gpu_scan_scratch,
                    shape_count
                );
            }

            // === Pass 3: Fill cell_shape_pairs ===
            {
                auto &srb = fill_cells_binding->GetShaderResourceBinding();
                srb.BindBuffer("AabbMin", *gpu_aabb_min);
                srb.BindBuffer("AabbMax", *gpu_aabb_max);
                srb.BindBuffer("GlobalFlags", *gpu_global_flags);
                srb.BindBuffer("GridConfig", *gpu_grid_config);
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("ShapeCellOffset", *gpu_shape_cell_offset);
                srb.BindBuffer("CellShapePairs", *gpu_cell_shape_pairs);

                auto *stage = fill_cells_stage.get();
                auto *binding = fill_cells_binding;
                uint32_t wg = (shape_count + 63u) / 64u;
                auto *scene_ptr = cached_scene;
                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("BH Fill Cells")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(aabb_min_h, RR)
                        .UseBuffer(aabb_max_h, RR)
                        .UseBuffer(global_h, RR)
                        .UseBuffer(gcfg_h, RR)
                        .UseBuffer(scount_h, RR)
                        .UseBuffer(sco_h, RR)
                        .UseBuffer(csp_h, WW)
                        .SetPassFunction(
                            [stage, binding, wg, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!scene_ptr->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding);
                                cb.DispatchCompute(wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // === Clear cell histogram ===
            {
                ComputeResourceBinding &bind = memset_stage->AllocateResourceBinding();
                auto &srb = bind.GetShaderResourceBinding();
                srb.BindBuffer("Target", *gpu_cell_histogram);
                srb.BindBuffer("ElemCount", *gpu_grid_cells_p1);

                auto *stage = memset_stage.get();
                auto *binding_ptr = &bind;
                uint32_t wg = (grid_total_cells + 1u + 63u) / 64u;
                auto *scene_ptr = cached_scene;
                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("BH Clear Histogram")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(hist_h, WW)
                        .UseBuffer(cells_p1_h, RR)
                        .SetPassFunction(
                            [stage, binding_ptr, wg, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!scene_ptr->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding_ptr);
                                cb.DispatchCompute(wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // === Pass 4: Histogram ===
            {
                auto &srb = histogram_binding->GetShaderResourceBinding();
                srb.BindBuffer("CellShapePairs", *gpu_cell_shape_pairs);
                srb.BindBuffer("CellHistogram", *gpu_cell_histogram);
                srb.BindBuffer("TotalAssignments", *gpu_total_assignments);

                auto *stage = histogram_stage.get();
                auto *binding = histogram_binding;
                uint32_t wg = (max_assignment_pairs + 63u) / 64u;
                auto *scene_ptr = cached_scene;
                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("BH Histogram")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(csp_h, RR)
                        .UseBuffer(hist_h, WW)
                        .UseBuffer(total_h, RR)
                        .SetPassFunction(
                            [stage, binding, wg, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!scene_ptr->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding);
                                cb.DispatchCompute(wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // === Prefix sum: cell_histogram → cell_offsets (in-place) ===
            {
                scan->AddPasses(
                    builder,
                    hist_h,
                    hist_h,
                    *gpu_cell_histogram,
                    *gpu_cell_histogram,
                    scan_scratch_h,
                    *gpu_scan_scratch,
                    grid_total_cells + 1u
                );
            }

            // === Copy cell_offsets → cell_scratch (initialize atomic counters) ===
            {
                ComputeResourceBinding &bind = copy_stage->AllocateResourceBinding();
                auto &srb = bind.GetShaderResourceBinding();
                srb.BindBuffer("SrcBuffer", *gpu_cell_histogram);
                srb.BindBuffer("DstBuffer", *gpu_cell_scratch);
                srb.BindBuffer("ElemCount", *gpu_grid_cells_p1);

                auto *stage = copy_stage.get();
                auto *binding_ptr = &bind;
                uint32_t wg = (grid_total_cells + 1u + 63u) / 64u;
                auto *scene_ptr = cached_scene;
                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("BH Copy Offsets -> Scratch")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(hist_h, RR)
                        .UseBuffer(cscr_h, WW)
                        .UseBuffer(cells_p1_h, RR)
                        .SetPassFunction(
                            [stage, binding_ptr, wg, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!scene_ptr->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding_ptr);
                                cb.DispatchCompute(wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // === Pass 5: Scatter sort ===
            {
                auto &srb = scatter_sort_binding->GetShaderResourceBinding();
                srb.BindBuffer("CellShapePairs", *gpu_cell_shape_pairs);
                srb.BindBuffer("SortedPairs", *gpu_sorted_pairs);
                srb.BindBuffer("CellScratch", *gpu_cell_scratch);
                srb.BindBuffer("TotalAssignments", *gpu_total_assignments);

                auto *stage = scatter_sort_stage.get();
                auto *binding = scatter_sort_binding;
                uint32_t wg = (max_assignment_pairs + 63u) / 64u;
                auto *scene_ptr = cached_scene;
                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("BH Scatter Sort")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(csp_h, RR)
                        .UseBuffer(sorted_h, WW)
                        .UseBuffer(cscr_h, RW)
                        .UseBuffer(total_h, RR)
                        .SetPassFunction(
                            [stage, binding, wg, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!scene_ptr->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding);
                                cb.DispatchCompute(wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // Clear pair count before generation.
            {
                ComputeResourceBinding &cbind = memset_stage->AllocateResourceBinding();
                AddClearPass(builder, cbind, pcnt_h, one_h, *gpu_pair_count, *gpu_one, "BH Clear PairCount");
            }

            // === Pass 6: Generate collision pairs ===
            {
                auto &srb = generate_pairs_binding->GetShaderResourceBinding();
                srb.BindBuffer("SortedPairs", *gpu_sorted_pairs);
                srb.BindBuffer("CellOffsets", *gpu_cell_histogram); // holds offsets after scan
                srb.BindBuffer("GlobalFlags", *gpu_global_flags);
                srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("CollisionPairs", *gpu_collision_pairs);
                srb.BindBuffer("PairCount", *gpu_pair_count);
                srb.BindBuffer("GridConfig", *gpu_grid_config);
                srb.BindBuffer("TotalAssignments", *gpu_total_assignments);
                srb.BindBuffer("ShapeFilterOffset", *gpu.shape_filter_offset);
                srb.BindBuffer("ShapeFilterCount", *gpu.shape_filter_count);
                srb.BindBuffer("ShapeFilterData", *gpu.shape_filter_data);
                srb.BindBuffer("AabbMin", *gpu_aabb_min);
                srb.BindBuffer("AabbMax", *gpu_aabb_max);

                auto *stage = generate_pairs_stage.get();
                auto *binding = generate_pairs_binding;
                uint32_t wg = (grid_total_cells + 63u) / 64u;
                auto *scene_ptr = cached_scene;
                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("BH Generate Pairs")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(sorted_h, RR)
                        .UseBuffer(coff_h, RR)
                        .UseBuffer(global_h, RR)
                        .UseBuffer(shape_alive_h, RR)
                        .UseBuffer(scount_h, RR)
                        .UseBuffer(pairs_h, WW)
                        .UseBuffer(pcnt_h, RW)
                        .UseBuffer(gcfg_h, RR)
                        .UseBuffer(total_h, RR)
                        .UseBuffer(filt_off_h, RR)
                        .UseBuffer(filt_cnt_h, RR)
                        .UseBuffer(filt_dat_h, RR)
                        .UseBuffer(aabb_min_h, RR)
                        .UseBuffer(aabb_max_h, RR)
                        .SetPassFunction(
                            [stage, binding, wg, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!scene_ptr->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding);
                                cb.DispatchCompute(wg, 1, 1);
                            }
                        )
                        .Get()
                );
            }

            // === Pass 7: Generate global pairs ===
            {
                auto &srb = global_pairs_binding->GetShaderResourceBinding();
                srb.BindBuffer("GlobalList", *gpu_global_list);
                srb.BindBuffer("GlobalCount", *gpu_global_count);
                srb.BindBuffer("GlobalFlags", *gpu_global_flags);
                srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("CollisionPairs", *gpu_collision_pairs);
                srb.BindBuffer("PairCount", *gpu_pair_count);
                srb.BindBuffer("ShapeFilterOffset", *gpu.shape_filter_offset);
                srb.BindBuffer("ShapeFilterCount", *gpu.shape_filter_count);
                srb.BindBuffer("ShapeFilterData", *gpu.shape_filter_data);
                srb.BindBuffer("AabbMin", *gpu_aabb_min);
                srb.BindBuffer("AabbMax", *gpu_aabb_max);

                auto *stage = global_pairs_stage.get();
                auto *binding = global_pairs_binding;
                uint32_t n_wg = (shape_count + 63u) / 64u;
                uint32_t g_wg = max_global_shape_count;
                auto *scene_ptr = cached_scene;
                builder.AddPass(
                    RenderGraphPassBuilder{render_system}
                        .SetName("BH Global Pairs")
                        .SetAffinity(RenderGraphPassAffinity::Compute)
                        .UseBuffer(global_list_h, RR)
                        .UseBuffer(global_count_h, RR)
                        .UseBuffer(global_h, RR)
                        .UseBuffer(shape_alive_h, RR)
                        .UseBuffer(scount_h, RR)
                        .UseBuffer(pairs_h, WW)
                        .UseBuffer(pcnt_h, RW)
                        .UseBuffer(filt_off_h, RR)
                        .UseBuffer(filt_cnt_h, RR)
                        .UseBuffer(filt_dat_h, RR)
                        .UseBuffer(aabb_min_h, RR)
                        .UseBuffer(aabb_max_h, RR)
                        .SetPassFunction(
                            [stage, binding, n_wg, g_wg, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                if (!scene_ptr->IsSimulationEnabled()) return;
                                cb.BindComputeStage(*stage);
                                cb.BindComputeResource(*binding);
                                cb.DispatchCompute(n_wg, g_wg, 1);
                            }
                        )
                        .Get()
                );
            }

            // === Dedup: RadixSort + CompactUnique ===
            // Import dedup buffers and add sort + unique passes after pair generation.
            {
                // Lazy-create algorithm instances.
                if (!radix_sort || radix_sort->GetMaxElemCount() < max_assignment_pairs) {
                    radix_sort = std::make_unique<RadixSort>(render_system, max_assignment_pairs);
                }
                if (!compact_unique || compact_unique->GetMaxElemCount() < max_assignment_pairs) {
                    compact_unique = std::make_unique<CompactUnique>(render_system, max_assignment_pairs);
                }

                // Sort pairs by (a, b).
                radix_sort->AddPasses(
                    builder,
                    pairs_h,
                    temp_pairs_h,
                    *gpu_collision_pairs,
                    *gpu_pairs_temp,
                    radix_scratch_h,
                    *gpu_radix_scratch,
                    max_assignment_pairs,         // elem_capacity (buffer size, for dispatch sizing)
                    pcnt_h,            // pair_count handle (RG tracks dependency)
                    *gpu_pair_count,   // pair_count buffer (read at GPU execution time)
                    cached_shape_count // max_shape_count for validation
                );

                // Compact unique.
                compact_unique->AddPasses(
                    builder,
                    pairs_h,
                    *gpu_collision_pairs,
                    unique_flags_h,
                    *gpu_unique_flags,
                    unique_offsets_h,
                    *gpu_unique_offsets,
                    unique_count_h,
                    *gpu_unique_count,
                    scan_scratch_h,
                    *gpu_scan_scratch,
                    *scan,
                    pcnt_h,          // pair_count handle
                    *gpu_pair_count, // pair_count buffer (read at GPU execution time)
                    max_assignment_pairs        // elem_capacity
                );

                // Copy unique_count back to pair_count so downstream
                // consumers see the deduplicated count.
                {
                    ComputeResourceBinding &cbind = copy_stage->AllocateResourceBinding();
                    auto &srb = cbind.GetShaderResourceBinding();
                    srb.BindBuffer("SrcBuffer", *gpu_unique_count);
                    srb.BindBuffer("DstBuffer", *gpu_pair_count);
                    srb.BindBuffer("ElemCount", *gpu_one);

                    auto *stage = copy_stage.get();
                    auto *binding_ptr = &cbind;
                    auto *scene_ptr = cached_scene;
                    builder.AddPass(
                        RenderGraphPassBuilder{render_system}
                            .SetName("BH Copy UniqueCount -> PairCount")
                            .SetAffinity(RenderGraphPassAffinity::Compute)
                            .UseBuffer(unique_count_h, RR)
                            .UseBuffer(pcnt_h, WW)
                            .UseBuffer(one_h, RR)
                            .SetPassFunction(
                                [stage, binding_ptr, scene_ptr](CommandBuffer &cb, const RenderGraph &) -> void {
                                    if (!scene_ptr->IsSimulationEnabled()) return;
                                    cb.BindComputeStage(*stage);
                                    cb.BindComputeResource(*binding_ptr);
                                    cb.DispatchCompute(1, 1, 1);
                                }
                            )
                            .Get()
                    );
                }
            }

            return builder.BuildRenderGraph();
        }
    };

    // ===================================================================
    // Public API
    // ===================================================================

    SpatialHashBroadDetector::SpatialHashBroadDetector(RenderSystem &render_system) :
        m_impl(std::make_unique<Impl>(render_system)) {
    }

    SpatialHashBroadDetector::~SpatialHashBroadDetector() = default;

    bool SpatialHashBroadDetector::IsInitialized() const noexcept {
        return m_impl->shaders_loaded;
    }

    BroadDetectorOutputBuffers SpatialHashBroadDetector::GetResultBuffers() const noexcept {
        return {
            .pair_buffer = m_impl->gpu_collision_pairs.get(),
            .pair_count_buffer = m_impl->gpu_pair_count.get(),
            .max_pairs = m_impl->max_assignment_pairs
        };
    }

    uint32_t SpatialHashBroadDetector::GetMaxPairs() const noexcept {
        return m_impl->max_assignment_pairs;
    }

    void SpatialHashBroadDetector::Configure(
        PhysicsScene &scene,
        uint32_t shape_count,
        const GridConfig &grid_config,
        uint32_t fallback_all_pairs_threshold,
        uint32_t max_global_shape_count
    ) {
        m_impl->cached_scene = &scene;
        m_impl->cached_shape_count = shape_count;
        m_impl->grid_config = grid_config;
        m_impl->fallback_threshold = fallback_all_pairs_threshold;
        m_impl->max_global_shape_count = max_global_shape_count;
        m_impl->max_assignment_pairs = shape_count * std::max(1u, grid_config.max_cells_per_shape + max_global_shape_count);

        // Validate and compute grid dimensions.
        glm::vec3 extent = grid_config.world_max - grid_config.world_min;
        m_impl->grid_dims.x = static_cast<int>(glm::ceil(extent.x / grid_config.cell_size));
        m_impl->grid_dims.y = static_cast<int>(glm::ceil(extent.y / grid_config.cell_size));
        m_impl->grid_dims.z = static_cast<int>(glm::ceil(extent.z / grid_config.cell_size));
        m_impl->grid_dims = glm::max(m_impl->grid_dims, glm::ivec3(1));
        uint64_t total = static_cast<uint64_t>(m_impl->grid_dims.x) * static_cast<uint64_t>(m_impl->grid_dims.y)
                         * static_cast<uint64_t>(m_impl->grid_dims.z);
        if (total > (1ull << 20)) {
            throw std::runtime_error(
                "SpatialHashBroadDetector: grid has " + std::to_string(total)
                + " cells (> 2^20). Increase cell_size or reduce world bounds."
            );
        }
        m_impl->grid_total_cells = static_cast<uint32_t>(total);

        if (shape_count > 1u) {
            m_impl->EnsureAllBuffers(shape_count);
        }

        // Upload CPU data to GPU-visible buffers.
        if (m_impl->gpu_shape_slot_count) {
            auto *addr = reinterpret_cast<uint32_t *>(m_impl->gpu_shape_slot_count->GetVMAddress());
            *addr = shape_count;
        }
        m_impl->UpdateGridConfigGpu();

        m_impl->EnsureShadersLoaded();
    }

    BroadDetectorOutputBuffers SpatialHashBroadDetector::Detect(vk::CommandBuffer cb) {
        assert(m_impl->cached_scene && "Configure must be called before Detect");
        const auto gpu = m_impl->cached_scene->GetGpuBuffers();

        if (gpu.shape_alive == nullptr || gpu.shape_world_position == nullptr || m_impl->cached_shape_count <= 1u) {
            BroadDetectorOutputBuffers empty;
            empty.max_pairs = m_impl->max_assignment_pairs;
            return empty;
        }

        // Lazy RG creation / rebuild when shape_count or fallback threshold changes.
        bool use_fallback = (m_impl->cached_shape_count <= m_impl->fallback_threshold);
        if (!m_impl->render_graph || m_impl->cached_shape_count != m_impl->cached_rg_shape_count
            || use_fallback != m_impl->cached_rg_use_fallback) {
            m_impl->render_graph = m_impl->BuildRenderGraph();
            m_impl->cached_rg_shape_count = m_impl->cached_shape_count;
            m_impl->cached_rg_use_fallback = use_fallback;
        }

        if (m_impl->render_graph && m_impl->render_graph->GetNumPasses() > 0) {
            m_impl->render_graph->RecordAllPasses(cb);
        }

        return {
            .pair_buffer = m_impl->gpu_collision_pairs.get(),
            .pair_count_buffer = m_impl->gpu_pair_count.get(),
            .max_pairs = m_impl->max_assignment_pairs
        };
    }
} // namespace Engine
