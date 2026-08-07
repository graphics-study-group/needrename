#include "SpatialHashBroadDetector.h"

#include <Physics/gpu_algorithm/CompactUnique.h>
#include <Physics/gpu_algorithm/ParallelScan.h>
#include <Physics/gpu_algorithm/RadixSort.h>

#include <cmake_config.h>

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsScene.h>
#include <Rhi/ComputeBuffer.h>
#include <Rhi/ComputeHelpers.h>
#include <Rhi/ComputeResourceBinding.h>
#include <Rhi/ComputeStage.h>
#include <Rhi/DeviceBuffer.h>
#include <Rhi/DeviceContext.h>
#include <Rhi/ShaderResourceBinding.h>

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

    const vk::MemoryBarrier2 kComputeBarrier{
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
    };
} // namespace

namespace Engine {

    struct alignas(16) GridConfigGpu {
        glm::vec4 world_min;
        glm::ivec4 grid_dims;
        uint32_t total_cells;
        uint32_t _pad[3];
    };
    static_assert(sizeof(GridConfigGpu) == 48, "GridConfigGpu must match shader UBO layout");

    struct SpatialHashBroadDetector::Impl {
        Rhi::DeviceContext &device_context;
        GridConfig grid_config{};
        uint32_t fallback_threshold = 8;
        uint32_t max_global_shape_count = 100;

        PhysicsScene *scene = nullptr;
        uint32_t shape_count = 0;
        uint32_t max_cell_shape_pair_count = 0;
        uint32_t max_output_pair_count = 0;

        bool shaders_loaded = false;

        uint32_t grid_total_cells = 0;
        glm::ivec3 grid_dims{};

        // ---- Compute stages ----
        std::unique_ptr<Rhi::ComputeStage> aabb_stage{};
        std::unique_ptr<Rhi::ComputeStage> count_cells_stage{};
        std::unique_ptr<Rhi::ComputeStage> fill_cells_stage{};
        std::unique_ptr<Rhi::ComputeStage> histogram_stage{};
        std::unique_ptr<Rhi::ComputeStage> scatter_sort_stage{};
        std::unique_ptr<Rhi::ComputeStage> generate_pairs_stage{};
        std::unique_ptr<Rhi::ComputeStage> fallback_pairs_stage{};
        std::unique_ptr<Rhi::ComputeStage> global_pairs_stage{};
        std::unique_ptr<Rhi::ComputeStage> memset_stage{};
        std::unique_ptr<Rhi::ComputeStage> copy_stage{};

        // ---- Pre-allocated bindings ----
        Rhi::ComputeResourceBinding *aabb_binding = nullptr;
        Rhi::ComputeResourceBinding *count_cells_binding = nullptr;
        Rhi::ComputeResourceBinding *fill_cells_binding = nullptr;
        Rhi::ComputeResourceBinding *histogram_binding = nullptr;
        Rhi::ComputeResourceBinding *scatter_sort_binding = nullptr;
        Rhi::ComputeResourceBinding *generate_pairs_binding = nullptr;
        Rhi::ComputeResourceBinding *fallback_pairs_binding = nullptr;
        Rhi::ComputeResourceBinding *global_pairs_binding = nullptr;
        Rhi::ComputeResourceBinding *memset_binding = nullptr;
        Rhi::ComputeResourceBinding *copy_binding = nullptr;

        // ---- Owned GPU buffers ----
        std::unique_ptr<Rhi::ComputeBuffer> gpu_aabb_min{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_aabb_max{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_shape_cell_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_shape_cell_offset{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_cell_shape_pairs{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_total_assignments{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_cell_histogram{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_cell_offsets{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_cell_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_cell_shape_pairs_sorted{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_global_flags{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_global_list{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_global_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_pairs{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_pair_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_grid_config{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_shape_slot_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_one{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_grid_cells_p1{};

        std::unique_ptr<Rhi::ComputeBuffer> gpu_pairs_temp{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_radix_scratch{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_unique_flags{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_unique_offsets{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_unique_count{};

        std::unique_ptr<ParallelScan> scan{};
        std::unique_ptr<RadixSort> radix_sort{};
        std::unique_ptr<CompactUnique> compact_unique{};

        std::unique_ptr<Rhi::ComputeBuffer> gpu_scan_scratch{};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        void EnsureBuffer(
            std::unique_ptr<Rhi::ComputeBuffer> &buf, size_t bytes, const char *name, bool host_visible = false
        ) {
            const auto &alloc = device_context.GetAllocatorState();
            if (!buf || buf->GetSize() != bytes) {
                buf = Rhi::ComputeBuffer::CreateUnique(alloc, bytes, host_visible, false, false, false, name);
            }
        }

        void EnsureAllBuffers(uint32_t shape_count) {
            const auto &alloc = device_context.GetAllocatorState();

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
                gpu_cell_shape_pairs,
                static_cast<size_t>(max_cell_shape_pair_count) * sizeof(glm::uvec2),
                "BH CellShapePairs"
            );
            EnsureBuffer(
                gpu_cell_shape_pairs_sorted,
                static_cast<size_t>(max_cell_shape_pair_count) * sizeof(glm::uvec2),
                "BH CellShapePairsSorted"
            );

            size_t cell_uint1 = static_cast<size_t>(grid_total_cells + 1u) * sizeof(uint32_t);
            EnsureBuffer(gpu_cell_histogram, cell_uint1, "BH CellHist");
            EnsureBuffer(gpu_cell_offsets, cell_uint1, "BH CellOffsets");
            EnsureBuffer(gpu_cell_scratch, cell_uint1, "BH CellScratch");

            EnsureBuffer(
                gpu_collision_pairs,
                static_cast<size_t>(max_output_pair_count) * sizeof(glm::uvec2),
                "BH Output CollisionPairs"
            );
            EnsureBuffer(gpu_pair_count, sizeof(uint32_t), "BH PairCount", true);

            {
                const size_t list_bytes = static_cast<size_t>(std::max(1u, shape_count)) * sizeof(uint32_t);
                if (!gpu_global_list || gpu_global_list->GetSize() < list_bytes) {
                    gpu_global_list = Rhi::ComputeBuffer::CreateUnique(
                        alloc, list_bytes, false, false, false, false, "BH GlobalList"
                    );
                }
            }
            if (!gpu_global_count || gpu_global_count->GetSize() < sizeof(uint32_t)) {
                gpu_global_count = Rhi::ComputeBuffer::CreateUnique(
                    alloc, sizeof(uint32_t), true, false, false, false, "BH GlobalCount"
                );
            }

            if (!gpu_one || gpu_one->GetSize() < sizeof(uint32_t)) {
                gpu_one =
                    Rhi::ComputeBuffer::CreateUnique(alloc, sizeof(uint32_t), true, false, false, false, "BH One");
                auto *addr = reinterpret_cast<uint32_t *>(gpu_one->GetVMAddress());
                *addr = 1u;
            }

            {
                const size_t temp_bytes = static_cast<size_t>(max_output_pair_count) * sizeof(glm::uvec2);
                EnsureBuffer(gpu_pairs_temp, temp_bytes, "BH PairsTemp");
            }
            EnsureBuffer(gpu_radix_scratch, RadixSort::GetRequiredScratchBytes(), "BH RadixScratch");
            {
                const size_t flags_bytes = CompactUnique::GetRequiredFlagBytes(max_output_pair_count);
                EnsureBuffer(gpu_unique_flags, flags_bytes, "BH UniqueFlags");
                EnsureBuffer(gpu_unique_offsets, flags_bytes, "BH UniqueOffsets");
            }
            if (!gpu_unique_count || gpu_unique_count->GetSize() < sizeof(uint32_t)) {
                gpu_unique_count = Rhi::ComputeBuffer::CreateUnique(
                    alloc, sizeof(uint32_t), true, false, false, false, "BH UniqueCount"
                );
            }

            if (!gpu_grid_cells_p1 || gpu_grid_cells_p1->GetSize() < sizeof(uint32_t)) {
                gpu_grid_cells_p1 = Rhi::ComputeBuffer::CreateUnique(
                    alloc, sizeof(uint32_t), true, false, false, false, "BH GridCellsP1"
                );
                auto *addr = reinterpret_cast<uint32_t *>(gpu_grid_cells_p1->GetVMAddress());
                *addr = grid_total_cells + 1u;
            }

            EnsureBuffer(gpu_grid_config, sizeof(GridConfigGpu), "BH GridConfig", true);

            {
                size_t scratch_bytes = ParallelScan::GetRequiredBlockSumsBytes(grid_total_cells);
                EnsureBuffer(gpu_scan_scratch, scratch_bytes, "BH ScanScratch");
            }
        }

        void EnsureShadersAndBindings() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            auto load_stage = [this](const char *path, const char *name) {
                auto spirv = LoadPhysicsSpirvBytes(path);
                auto stage = std::make_unique<Rhi::ComputeStage>(device_context);
                stage->Instantiate(spirv, name);
                return stage;
            };

            aabb_stage = load_stage("collision/SpatialHashBroadDetector/compute_aabbs.comp.spv", "BH ComputeAABBs");
            aabb_binding = &aabb_stage->AllocateResourceBinding();

            count_cells_stage = load_stage("collision/SpatialHashBroadDetector/count_cells.comp.spv", "BH CountCells");
            count_cells_binding = &count_cells_stage->AllocateResourceBinding();

            fill_cells_stage = load_stage("collision/SpatialHashBroadDetector/fill_cells.comp.spv", "BH FillCells");
            fill_cells_binding = &fill_cells_stage->AllocateResourceBinding();

            histogram_stage = load_stage("collision/SpatialHashBroadDetector/histogram_cells.comp.spv", "BH Histogram");
            histogram_binding = &histogram_stage->AllocateResourceBinding();

            scatter_sort_stage =
                load_stage("collision/SpatialHashBroadDetector/scatter_sort.comp.spv", "BH ScatterSort");
            scatter_sort_binding = &scatter_sort_stage->AllocateResourceBinding();

            generate_pairs_stage =
                load_stage("collision/SpatialHashBroadDetector/generate_broad_pairs.comp.spv", "BH GenPairs");
            generate_pairs_binding = &generate_pairs_stage->AllocateResourceBinding();

            fallback_pairs_stage = load_stage(
                "collision/SpatialHashBroadDetector/generate_all_pairs_fallback.comp.spv", "BH FallbackPairs"
            );
            fallback_pairs_binding = &fallback_pairs_stage->AllocateResourceBinding();

            global_pairs_stage =
                load_stage("collision/SpatialHashBroadDetector/generate_global_pairs.comp.spv", "BH GlobalPairs");
            global_pairs_binding = &global_pairs_stage->AllocateResourceBinding();

            memset_stage = load_stage("collision/SpatialHashBroadDetector/memset_uint.comp.spv", "BH Memset");
            memset_binding = &memset_stage->AllocateResourceBinding();

            copy_stage = load_stage("collision/SpatialHashBroadDetector/copy_uint.comp.spv", "BH Copy");
            copy_binding = &copy_stage->AllocateResourceBinding();
        }

        // -----------------------------------------------------------------
        // Dispatch helpers
        // -----------------------------------------------------------------

        void DispatchClear(
            vk::CommandBuffer cb, uint32_t frame, Rhi::ComputeBuffer &target, Rhi::ComputeBuffer &elem_count
        ) {
            auto &srb = memset_binding->GetShaderResourceBinding();
            srb.BindBuffer("Target", target);
            srb.BindBuffer("ElemCount", elem_count);
            Rhi::BindComputeStage(cb, *memset_stage);
            Rhi::BindComputeResource(cb, *memset_stage, *memset_binding, frame);
            uint32_t wg = (target.GetSize() / sizeof(uint32_t) + 63u) / 64u;
            if (wg == 0) wg = 1;
            Rhi::DispatchCompute(cb, wg, 1, 1);
        }

        void DispatchCopy(
            vk::CommandBuffer cb,
            uint32_t frame,
            Rhi::ComputeBuffer &src,
            Rhi::ComputeBuffer &dst,
            Rhi::ComputeBuffer &elem_count
        ) {
            auto &srb = copy_binding->GetShaderResourceBinding();
            srb.BindBuffer("SrcBuffer", src);
            srb.BindBuffer("DstBuffer", dst);
            srb.BindBuffer("ElemCount", elem_count);
            Rhi::BindComputeStage(cb, *copy_stage);
            Rhi::BindComputeResource(cb, *copy_stage, *copy_binding, frame);
            uint32_t wg = (dst.GetSize() / sizeof(uint32_t) + 63u) / 64u;
            if (wg == 0) wg = 1;
            Rhi::DispatchCompute(cb, wg, 1, 1);
        }

        // -----------------------------------------------------------------
        // Recording paths
        // -----------------------------------------------------------------

        void RecordAABBPass(vk::CommandBuffer cb, uint32_t frame) {
            const auto gpu = scene->GetGpuBuffers();
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

            uint32_t wg = (shape_count + 63u) / 64u;
            Rhi::BindComputeStage(cb, *aabb_stage);
            Rhi::BindComputeResource(cb, *aabb_stage, *aabb_binding, frame);
            Rhi::DispatchCompute(cb, wg, 1, 1);
        }

        void RecordFallbackPath(vk::CommandBuffer cb, uint32_t frame) {
            const auto gpu = scene->GetGpuBuffers();

            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            DispatchClear(cb, frame, *gpu_global_count, *gpu_one);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            RecordAABBPass(cb, frame);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            DispatchClear(cb, frame, *gpu_pair_count, *gpu_one);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            auto &srb = fallback_pairs_binding->GetShaderResourceBinding();
            srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
            srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
            srb.BindBuffer("CollisionPairs", *gpu_collision_pairs);
            srb.BindBuffer("PairCount", *gpu_pair_count);
            srb.BindBuffer("ShapeFilterData", *gpu.shape_filter_data);
            srb.BindBuffer("AabbMin", *gpu_aabb_min);
            srb.BindBuffer("AabbMax", *gpu_aabb_max);

            uint32_t total_pairs = (shape_count * (shape_count - 1u)) / 2u;
            uint32_t wg = (total_pairs + 63u) / 64u;
            Rhi::BindComputeStage(cb, *fallback_pairs_stage);
            Rhi::BindComputeResource(cb, *fallback_pairs_stage, *fallback_pairs_binding, frame);
            Rhi::DispatchCompute(cb, wg, 1, 1);
        }

        void RecordSpatialHashPath(vk::CommandBuffer cb, uint32_t frame) {
            const auto gpu = scene->GetGpuBuffers();
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            DispatchClear(cb, frame, *gpu_global_count, *gpu_one);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            RecordAABBPass(cb, frame);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            DispatchClear(cb, frame, *gpu_total_assignments, *gpu_one);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Count cells
            {
                auto &srb = count_cells_binding->GetShaderResourceBinding();
                srb.BindBuffer("AabbMin", *gpu_aabb_min);
                srb.BindBuffer("AabbMax", *gpu_aabb_max);
                srb.BindBuffer("GlobalFlags", *gpu_global_flags);
                srb.BindBuffer("GridConfig", *gpu_grid_config);
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("ShapeCellCount", *gpu_shape_cell_count);
                srb.BindBuffer("TotalAssignments", *gpu_total_assignments);

                uint32_t wg = (shape_count + 63u) / 64u;
                Rhi::BindComputeStage(cb, *count_cells_stage);
                Rhi::BindComputeResource(cb, *count_cells_stage, *count_cells_binding, frame);
                Rhi::DispatchCompute(cb, wg, 1, 1);
            }
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Prefix sum: shape_cell_count -> shape_cell_offset
            scan->Record(cb, *gpu_shape_cell_count, *gpu_shape_cell_offset, *gpu_scan_scratch, shape_count);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Fill cells
            {
                auto &srb = fill_cells_binding->GetShaderResourceBinding();
                srb.BindBuffer("AabbMin", *gpu_aabb_min);
                srb.BindBuffer("AabbMax", *gpu_aabb_max);
                srb.BindBuffer("GlobalFlags", *gpu_global_flags);
                srb.BindBuffer("GridConfig", *gpu_grid_config);
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("ShapeCellOffset", *gpu_shape_cell_offset);
                srb.BindBuffer("CellShapePairs", *gpu_cell_shape_pairs);

                uint32_t wg = (shape_count + 63u) / 64u;
                Rhi::BindComputeStage(cb, *fill_cells_stage);
                Rhi::BindComputeResource(cb, *fill_cells_stage, *fill_cells_binding, frame);
                Rhi::DispatchCompute(cb, wg, 1, 1);
            }
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Clear histogram
            DispatchClear(cb, frame, *gpu_cell_histogram, *gpu_grid_cells_p1);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Histogram
            {
                auto &srb = histogram_binding->GetShaderResourceBinding();
                srb.BindBuffer("CellShapePairs", *gpu_cell_shape_pairs);
                srb.BindBuffer("CellHistogram", *gpu_cell_histogram);
                srb.BindBuffer("TotalAssignments", *gpu_total_assignments);

                uint32_t wg = (max_cell_shape_pair_count + 63u) / 64u;
                Rhi::BindComputeStage(cb, *histogram_stage);
                Rhi::BindComputeResource(cb, *histogram_stage, *histogram_binding, frame);
                Rhi::DispatchCompute(cb, wg, 1, 1);
            }
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Prefix sum: cell_histogram -> cell_histogram (in-place)
            scan->Record(cb, *gpu_cell_histogram, *gpu_cell_histogram, *gpu_scan_scratch, grid_total_cells + 1u);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Copy cell_offsets -> cell_scratch
            DispatchCopy(cb, frame, *gpu_cell_histogram, *gpu_cell_scratch, *gpu_grid_cells_p1);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Scatter sort
            {
                auto &srb = scatter_sort_binding->GetShaderResourceBinding();
                srb.BindBuffer("CellShapePairs", *gpu_cell_shape_pairs);
                srb.BindBuffer("SortedPairs", *gpu_cell_shape_pairs_sorted);
                srb.BindBuffer("CellScratch", *gpu_cell_scratch);
                srb.BindBuffer("TotalAssignments", *gpu_total_assignments);

                uint32_t wg = (max_cell_shape_pair_count + 63u) / 64u;
                Rhi::BindComputeStage(cb, *scatter_sort_stage);
                Rhi::BindComputeResource(cb, *scatter_sort_stage, *scatter_sort_binding, frame);
                Rhi::DispatchCompute(cb, wg, 1, 1);
            }
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Clear pair count
            DispatchClear(cb, frame, *gpu_pair_count, *gpu_one);
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Generate pairs
            {
                auto &srb = generate_pairs_binding->GetShaderResourceBinding();
                srb.BindBuffer("SortedPairs", *gpu_cell_shape_pairs_sorted);
                srb.BindBuffer("CellOffsets", *gpu_cell_histogram);
                srb.BindBuffer("GlobalFlags", *gpu_global_flags);
                srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("CollisionPairs", *gpu_collision_pairs);
                srb.BindBuffer("PairCount", *gpu_pair_count);
                srb.BindBuffer("GridConfig", *gpu_grid_config);
                srb.BindBuffer("TotalAssignments", *gpu_total_assignments);
                srb.BindBuffer("ShapeFilterData", *gpu.shape_filter_data);
                srb.BindBuffer("AabbMin", *gpu_aabb_min);
                srb.BindBuffer("AabbMax", *gpu_aabb_max);

                uint32_t wg = (grid_total_cells + 63u) / 64u;
                Rhi::BindComputeStage(cb, *generate_pairs_stage);
                Rhi::BindComputeResource(cb, *generate_pairs_stage, *generate_pairs_binding, frame);
                Rhi::DispatchCompute(cb, wg, 1, 1);
            }
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Generate global pairs
            {
                auto &srb = global_pairs_binding->GetShaderResourceBinding();
                srb.BindBuffer("GlobalList", *gpu_global_list);
                srb.BindBuffer("GlobalCount", *gpu_global_count);
                srb.BindBuffer("GlobalFlags", *gpu_global_flags);
                srb.BindBuffer("ShapeAlive", *gpu.shape_alive);
                srb.BindBuffer("ShapeSlotCount", *gpu_shape_slot_count);
                srb.BindBuffer("CollisionPairs", *gpu_collision_pairs);
                srb.BindBuffer("PairCount", *gpu_pair_count);
                srb.BindBuffer("ShapeFilterData", *gpu.shape_filter_data);
                srb.BindBuffer("AabbMin", *gpu_aabb_min);
                srb.BindBuffer("AabbMax", *gpu_aabb_max);

                uint32_t n_wg = (shape_count + 63u) / 64u;
                Rhi::BindComputeStage(cb, *global_pairs_stage);
                Rhi::BindComputeResource(cb, *global_pairs_stage, *global_pairs_binding, frame);
                Rhi::DispatchCompute(cb, n_wg, max_global_shape_count, 1);
            }
            cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

            // Dedup: RadixSort + CompactUnique
            {
                radix_sort->Record(
                    cb,
                    *gpu_collision_pairs,
                    *gpu_pairs_temp,
                    *gpu_radix_scratch,
                    max_output_pair_count,
                    *gpu_pair_count,
                    shape_count
                );
                cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

                compact_unique->Record(
                    cb,
                    *gpu_collision_pairs,
                    *gpu_unique_flags,
                    *gpu_unique_offsets,
                    *gpu_unique_count,
                    *gpu_scan_scratch,
                    *scan,
                    *gpu_pair_count,
                    max_output_pair_count
                );
                cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

                DispatchCopy(cb, frame, *gpu_unique_count, *gpu_pair_count, *gpu_one);
            }
        }
    };

    // ===================================================================
    // Public API
    // ===================================================================

    SpatialHashBroadDetector::SpatialHashBroadDetector(Rhi::DeviceContext &device_context) :
        m_impl(std::make_unique<Impl>(device_context)) {
    }

    SpatialHashBroadDetector::~SpatialHashBroadDetector() = default;

    bool SpatialHashBroadDetector::IsInitialized() const noexcept {
        return m_impl->shaders_loaded;
    }

    uint32_t SpatialHashBroadDetector::GetMaxPairs() const noexcept {
        return m_impl->max_output_pair_count;
    }

    BroadDetectorOutputBuffers SpatialHashBroadDetector::GetResultBuffers() const noexcept {
        return {
            .pair_buffer = m_impl->gpu_collision_pairs.get(),
            .pair_count_buffer = m_impl->gpu_pair_count.get(),
            .max_pairs = m_impl->max_output_pair_count
        };
    }

    void SpatialHashBroadDetector::Configure(
        PhysicsScene &scene,
        uint32_t shape_count,
        const GridConfig &grid_config,
        uint32_t fallback_all_pairs_threshold,
        uint32_t max_global_shape_count
    ) {
        m_impl->scene = &scene;
        m_impl->shape_count = shape_count;
        m_impl->grid_config = grid_config;
        m_impl->fallback_threshold = fallback_all_pairs_threshold;
        m_impl->max_global_shape_count = max_global_shape_count;

        // Compute grid dimensions.
        auto world_size = grid_config.world_max - grid_config.world_min;
        m_impl->grid_dims = glm::ivec3{
            glm::max(1, static_cast<int32_t>(glm::ceil(world_size.x / grid_config.cell_size))),
            glm::max(1, static_cast<int32_t>(glm::ceil(world_size.y / grid_config.cell_size))),
            glm::max(1, static_cast<int32_t>(glm::ceil(world_size.z / grid_config.cell_size)))
        };
        m_impl->grid_total_cells = static_cast<uint32_t>(m_impl->grid_dims.x)
                                   * static_cast<uint32_t>(m_impl->grid_dims.y)
                                   * static_cast<uint32_t>(m_impl->grid_dims.z);

        // Compute max pairs.
        uint32_t cell_capacity = std::max(1u, shape_count * grid_config.max_cells_per_shape);
        m_impl->max_cell_shape_pair_count =
            std::min(cell_capacity, 1u << 20); // cap at 1M to stay within RadixSort limits
        m_impl->max_output_pair_count =
            std::max(1u, std::min(shape_count * (shape_count - 1) / 2, 1u << 20)); // cap at 1M

        // Allocate buffers.
        m_impl->EnsureAllBuffers(shape_count);

        // Load shaders and allocate bindings.
        m_impl->EnsureShadersAndBindings();

        // Upload grid config to GPU.
        {
            auto *cfg = reinterpret_cast<GridConfigGpu *>(m_impl->gpu_grid_config->GetVMAddress());
            cfg->world_min = glm::vec4(grid_config.world_min, grid_config.cell_size);
            cfg->grid_dims = glm::ivec4(m_impl->grid_dims, static_cast<int32_t>(grid_config.max_cells_per_shape));
            cfg->total_cells = m_impl->grid_total_cells;
            cfg->_pad[0] = cfg->_pad[1] = cfg->_pad[2] = 0u;
        }
        auto *slot_addr = reinterpret_cast<uint32_t *>(m_impl->gpu_shape_slot_count->GetVMAddress());
        *slot_addr = shape_count;

        uint32_t scan_element = std::max(m_impl->max_output_pair_count, m_impl->grid_total_cells + 1u);
        if (!m_impl->scan || m_impl->scan->GetMaxElemCount() < scan_element) {
            m_impl->scan = std::make_unique<ParallelScan>(m_impl->device_context, scan_element);
        }
        m_impl->scan->ResetParamPool();

        if (!m_impl->radix_sort || m_impl->radix_sort->GetMaxElemCount() < m_impl->max_output_pair_count) {
            m_impl->radix_sort = std::make_unique<RadixSort>(m_impl->device_context, m_impl->max_output_pair_count);
        }
        m_impl->radix_sort->ResetParamPool();

        if (!m_impl->compact_unique || m_impl->compact_unique->GetMaxElemCount() < m_impl->max_output_pair_count) {
            m_impl->compact_unique =
                std::make_unique<CompactUnique>(m_impl->device_context, m_impl->max_output_pair_count);
        }
    }

    void SpatialHashBroadDetector::Record(vk::CommandBuffer cb) {
        const uint32_t frame = m_frame_counter++ % 3;
        assert(m_impl->scene && "Configure must be called before Record");
        const auto gpu = m_impl->scene->GetGpuBuffers();

        if (gpu.shape_alive == nullptr || gpu.shape_world_position == nullptr || m_impl->shape_count <= 1u) {
            return;
        }

        cb.pipelineBarrier2(vk::DependencyInfo{{}, {kComputeBarrier}, {}, {}});

        if (m_impl->shape_count <= m_impl->fallback_threshold) {
            m_impl->RecordFallbackPath(cb, frame);
        } else {
            m_impl->RecordSpatialHashPath(cb, frame);
        }
    }
} // namespace Engine
