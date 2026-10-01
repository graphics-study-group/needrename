#include "SpatialHashBroadDetector.h"

#include <Physics/gpu_algorithm/CompactUnique.h>
#include <Physics/gpu_algorithm/ParallelScan.h>
#include <Physics/gpu_algorithm/RadixSort.h>

#include <Physics/PhysicsDispatch.h>

#include <Physics/PhysicsSpirvLoader.h>

#include <vulkan/vulkan.hpp>

#include <Physics/PhysicsScene.h>
#include <Rhi/Buffer/ComputeBuffer.h>
#include <Rhi/Buffer/DeviceBuffer.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeKernel.h>

#include <cassert>
#include <vector>

namespace Engine {

    // Push-constant layout, matching the GridPush block in the SpatialHash
    // shaders. The reflected size is the declared size (std430 member layout,
    // no struct-level 16 padding): vec4 + ivec4 + uint = 36 bytes.
    struct GridPushParams {
        glm::vec4 world_min_cell_size;  // xyz = bounds min, w = cell_size
        glm::ivec4 grid_dims_max_cells; // xyz = grid dimensions, w = max_cells_per_shape
        uint32_t shape_slot_count;
    };
    static_assert(sizeof(GridPushParams) == 36, "GridPushParams must match shader push block");

    // Push-constant layout, matching generate_broad_pairs.comp's GridPush block:
    // the cell bound plus the shape count the pairs are packed with.
    struct BroadPairsPush {
        uint32_t total_cells;
        uint32_t shape_count;
    };
    static_assert(sizeof(BroadPairsPush) == 8, "BroadPairsPush must match shader push block");

    // The packed-key bound: `shape_count * (shape_count - 1)` already wraps in
    // 32-bit arithmetic above this, in this file and in generate_broad_pairs.comp,
    // so the packing would silently produce a wrong key order for a larger scene.
    // The limit is asserted at Configure time rather than discovered later.
    constexpr uint32_t kMaxPackableShapeCount = 65536u;

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
        /// True once the buffers, kernels and constants match `shape_count`.
        bool prepared = false;

        uint32_t grid_total_cells = 0;
        glm::ivec3 grid_dims{};

        // ---- Compute kernels (cached from the device, acquired in EnsureKernels) ----
        Rhi::ComputeKernel *aabb_kernel = nullptr;
        Rhi::ComputeKernel *count_cells_kernel = nullptr;
        Rhi::ComputeKernel *fill_cells_kernel = nullptr;
        Rhi::ComputeKernel *histogram_kernel = nullptr;
        Rhi::ComputeKernel *scatter_sort_kernel = nullptr;
        Rhi::ComputeKernel *generate_pairs_kernel = nullptr;
        Rhi::ComputeKernel *fallback_pairs_kernel = nullptr;
        Rhi::ComputeKernel *global_pairs_kernel = nullptr;
        Rhi::ComputeKernel *unpack_pairs_kernel = nullptr;
        Rhi::ComputeKernel *memset_kernel = nullptr;
        Rhi::ComputeKernel *copy_kernel = nullptr;

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
        // The dedup's key array (the pair generators' output, sorted and compacted
        // in place) and the detector's canonical `uvec2` pair output.
        std::unique_ptr<Rhi::ComputeBuffer> gpu_pair_keys{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_collision_pairs{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_pair_count{};
        std::unique_ptr<Rhi::ComputeBuffer> gpu_unique_count{};

        // CPU-side per-dispatch constants, recorded as push constants in Record.
        GridPushParams grid_push{};

        // Each algorithm owns its working storage, so the detector holds no scratch
        // buffer and only names the instances it uses.  The scan instance is the
        // detector's own: its results are what this detector consumes.
        std::unique_ptr<ParallelScan> scan{};
        std::unique_ptr<RadixSort> radix_sort{};
        std::unique_ptr<CompactUnique> compact_unique{};

        explicit Impl(Rhi::DeviceContext &ctx) : device_context(ctx) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        /// @brief Grow a detector buffer to at least `bytes`
        void EnsureBuffer(
            std::unique_ptr<Rhi::ComputeBuffer> &buf, size_t bytes, const char *name, bool host_visible = false
        ) {
            Rhi::EnsureComputeBuffer(buf, device_context.GetAllocatorState(), bytes, host_visible, name);
        }

        /// @brief Size every data buffer this preparation's geometry needs.
        void EnsureAllBuffers() {
            const size_t shape_vec4 = static_cast<size_t>(shape_count) * sizeof(glm::vec4);
            const size_t shape_uint = static_cast<size_t>(shape_count) * sizeof(uint32_t);

            EnsureBuffer(gpu_aabb_min, shape_vec4, "BH AabbMin");
            EnsureBuffer(gpu_aabb_max, shape_vec4, "BH AabbMax");
            EnsureBuffer(gpu_shape_cell_count, shape_uint, "BH ShapeCellCnt");
            EnsureBuffer(gpu_shape_cell_offset, shape_uint, "BH ShapeCellOff");
            EnsureBuffer(gpu_global_flags, shape_uint, "BH GlobalFlags");

            EnsureBuffer(gpu_total_assignments, sizeof(uint32_t), "BH TotalAssign");
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

            EnsureBuffer(gpu_pair_keys, static_cast<size_t>(max_output_pair_count) * sizeof(uint32_t), "BH PairKeys");
            EnsureBuffer(
                gpu_collision_pairs,
                static_cast<size_t>(max_output_pair_count) * sizeof(glm::uvec2),
                "BH Output CollisionPairs"
            );

            EnsureBuffer(gpu_pair_count, sizeof(uint32_t), "BH PairCount");
            {
                const size_t list_bytes = static_cast<size_t>(std::max(1u, shape_count)) * sizeof(uint32_t);
                EnsureBuffer(gpu_global_list, list_bytes, "BH GlobalList");
            }
            EnsureBuffer(gpu_global_count, sizeof(uint32_t), "BH GlobalCount");
            EnsureBuffer(gpu_unique_count, sizeof(uint32_t), "BH UniqueCount");
        }

        void EnsureKernels() {
            if (shaders_loaded) return;
            shaders_loaded = true;

            auto load_kernel = [this](const char *path, const char *name) {
                return &LoadPhysicsKernel(device_context, path, name);
            };

            aabb_kernel = load_kernel("collision/SpatialHashBroadDetector/compute_aabbs.comp.spv", "BH ComputeAABBs");
            count_cells_kernel =
                load_kernel("collision/SpatialHashBroadDetector/count_cells.comp.spv", "BH CountCells");
            fill_cells_kernel = load_kernel("collision/SpatialHashBroadDetector/fill_cells.comp.spv", "BH FillCells");
            histogram_kernel =
                load_kernel("collision/SpatialHashBroadDetector/histogram_cells.comp.spv", "BH Histogram");
            scatter_sort_kernel =
                load_kernel("collision/SpatialHashBroadDetector/scatter_sort.comp.spv", "BH ScatterSort");
            generate_pairs_kernel =
                load_kernel("collision/SpatialHashBroadDetector/generate_broad_pairs.comp.spv", "BH GenPairs");
            fallback_pairs_kernel = load_kernel(
                "collision/SpatialHashBroadDetector/generate_all_pairs_fallback.comp.spv", "BH FallbackPairs"
            );
            global_pairs_kernel =
                load_kernel("collision/SpatialHashBroadDetector/generate_global_pairs.comp.spv", "BH GlobalPairs");
            unpack_pairs_kernel =
                load_kernel("collision/SpatialHashBroadDetector/unpack_pairs.comp.spv", "BH UnpackPairs");
            memset_kernel = load_kernel("collision/SpatialHashBroadDetector/memset_uint.comp.spv", "BH Memset");
            copy_kernel = load_kernel("collision/SpatialHashBroadDetector/copy_uint_push.comp.spv", "BH Copy");
        }

        // -----------------------------------------------------------------
        // Dispatch helpers
        // -----------------------------------------------------------------

        void DispatchClear(vk::CommandBuffer cb, Rhi::ComputeBuffer &target, uint32_t elem_count) {
            uint32_t wg = (elem_count + 63u) / 64u;
            if (wg == 0) wg = 1;
            memset_kernel->Dispatch(cb, {{"Target", target}}, wg, 1, 1, elem_count);
        }

        void DispatchCopy(vk::CommandBuffer cb, Rhi::ComputeBuffer &src, Rhi::ComputeBuffer &dst, uint32_t elem_count) {
            uint32_t wg = (elem_count + 63u) / 64u;
            if (wg == 0) wg = 1;
            copy_kernel->Dispatch(cb, {{"SrcBuffer", src}, {"DstBuffer", dst}}, wg, 1, 1, elem_count);
        }

        // -----------------------------------------------------------------
        // Recording paths
        // -----------------------------------------------------------------

        void RecordAABBPass(vk::CommandBuffer cb) {
            const auto gpu = scene->GetGpuBuffers();

            uint32_t wg = (shape_count + 63u) / 64u;
            aabb_kernel->Dispatch(
                cb,
                {{"ShapeAlive", *gpu.shape_alive},
                 {"ShapeType", *gpu.shape_type},
                 {"ShapeFeature", *gpu.shape_feature},
                 {"ShapeWorldPosition", *gpu.shape_world_position},
                 {"ShapeWorldRotation", *gpu.shape_world_rotation},
                 {"AabbMin", *gpu_aabb_min},
                 {"AabbMax", *gpu_aabb_max},
                 {"GlobalFlags", *gpu_global_flags},
                 {"GlobalList", *gpu_global_list},
                 {"GlobalCount", *gpu_global_count}},
                wg,
                1,
                1,
                grid_push
            );
        }

        void RecordFallbackPath(vk::CommandBuffer cb) {
            const auto gpu = scene->GetGpuBuffers();

            DispatchClear(cb, *gpu_pair_count, 1u);
            DispatchBarrier(cb);

            uint32_t total_pairs = (shape_count * (shape_count - 1u)) / 2u;
            uint32_t wg = (total_pairs + 63u) / 64u;
            fallback_pairs_kernel->Dispatch(
                cb,
                {{"ShapeAlive", *gpu.shape_alive},
                 {"CollisionPairs", *gpu_collision_pairs},
                 {"PairCount", *gpu_pair_count},
                 {"ShapeFilterData", *gpu.shape_filter_data},
                 {"AabbMin", *gpu_aabb_min},
                 {"AabbMax", *gpu_aabb_max}},
                wg,
                1,
                1,
                shape_count
            );
        }

        void RecordSpatialHashPath(vk::CommandBuffer cb) {
            const auto gpu = scene->GetGpuBuffers();

            DispatchClear(cb, *gpu_total_assignments, 1u);
            DispatchBarrier(cb);

            // Count cells
            {
                uint32_t wg = (shape_count + 63u) / 64u;
                count_cells_kernel->Dispatch(
                    cb,
                    {{"AabbMin", *gpu_aabb_min},
                     {"AabbMax", *gpu_aabb_max},
                     {"GlobalFlags", *gpu_global_flags},
                     {"ShapeCellCount", *gpu_shape_cell_count},
                     {"TotalAssignments", *gpu_total_assignments}},
                    wg,
                    1,
                    1,
                    grid_push
                );
            }
            DispatchBarrier(cb);

            // Prefix sum: shape_cell_count -> shape_cell_offset
            scan->Record(cb, *gpu_shape_cell_count, *gpu_shape_cell_offset, shape_count);
            DispatchBarrier(cb);

            // Fill cells
            {
                uint32_t wg = (shape_count + 63u) / 64u;
                fill_cells_kernel->Dispatch(
                    cb,
                    {{"AabbMin", *gpu_aabb_min},
                     {"AabbMax", *gpu_aabb_max},
                     {"GlobalFlags", *gpu_global_flags},
                     {"ShapeCellOffset", *gpu_shape_cell_offset},
                     {"CellShapePairs", *gpu_cell_shape_pairs}},
                    wg,
                    1,
                    1,
                    grid_push
                );
            }
            DispatchBarrier(cb);

            // Clear histogram
            DispatchClear(cb, *gpu_cell_histogram, grid_total_cells + 1u);
            DispatchBarrier(cb);

            // Histogram
            {
                uint32_t wg = (max_cell_shape_pair_count + 63u) / 64u;
                histogram_kernel->Dispatch(
                    cb,
                    {{"CellShapePairs", *gpu_cell_shape_pairs},
                     {"CellHistogram", *gpu_cell_histogram},
                     {"TotalAssignments", *gpu_total_assignments}},
                    wg,
                    1,
                    1
                );
            }
            DispatchBarrier(cb);

            // Prefix sum: cell_histogram -> cell_histogram (in-place)
            scan->Record(cb, *gpu_cell_histogram, *gpu_cell_histogram, grid_total_cells + 1u);
            DispatchBarrier(cb);

            // Copy cell_offsets -> cell_scratch
            DispatchCopy(cb, *gpu_cell_histogram, *gpu_cell_scratch, grid_total_cells + 1u);
            DispatchBarrier(cb);

            // Scatter sort
            {
                uint32_t wg = (max_cell_shape_pair_count + 63u) / 64u;
                scatter_sort_kernel->Dispatch(
                    cb,
                    {{"CellShapePairs", *gpu_cell_shape_pairs},
                     {"SortedPairs", *gpu_cell_shape_pairs_sorted},
                     {"CellScratch", *gpu_cell_scratch},
                     {"TotalAssignments", *gpu_total_assignments}},
                    wg,
                    1,
                    1
                );
            }
            DispatchBarrier(cb);

            // Clear pair count
            DispatchClear(cb, *gpu_pair_count, 1u);
            DispatchBarrier(cb);

            // Generate pairs
            {
                uint32_t wg = (grid_total_cells + 63u) / 64u;
                generate_pairs_kernel->Dispatch(
                    cb,
                    {{"SortedPairs", *gpu_cell_shape_pairs_sorted},
                     {"CellOffsets", *gpu_cell_histogram},
                     {"GlobalFlags", *gpu_global_flags},
                     {"ShapeAlive", *gpu.shape_alive},
                     {"CollisionKeys", *gpu_pair_keys},
                     {"PairCount", *gpu_pair_count},
                     {"TotalAssignments", *gpu_total_assignments},
                     {"ShapeFilterData", *gpu.shape_filter_data},
                     {"AabbMin", *gpu_aabb_min},
                     {"AabbMax", *gpu_aabb_max}},
                    wg,
                    1,
                    1,
                    BroadPairsPush{grid_total_cells, shape_count}
                );
            }
            DispatchBarrier(cb);

            // Generate global pairs
            {
                uint32_t n_wg = (shape_count + 63u) / 64u;
                global_pairs_kernel->Dispatch(
                    cb,
                    {{"GlobalList", *gpu_global_list},
                     {"GlobalCount", *gpu_global_count},
                     {"GlobalFlags", *gpu_global_flags},
                     {"ShapeAlive", *gpu.shape_alive},
                     {"CollisionKeys", *gpu_pair_keys},
                     {"PairCount", *gpu_pair_count},
                     {"ShapeFilterData", *gpu.shape_filter_data},
                     {"AabbMin", *gpu_aabb_min},
                     {"AabbMax", *gpu_aabb_max}},
                    n_wg,
                    max_global_shape_count,
                    1,
                    shape_count
                );
            }
            DispatchBarrier(cb);

            // Dedup: sort the packed keys, compact the duplicates away
            {
                const RadixSortBuffers sort_buffers{
                    .keys = gpu_pair_keys.get(),
                    .payload = nullptr,
                    .count = gpu_pair_count.get(),
                };
                radix_sort->Record(cb, sort_buffers, max_output_pair_count, shape_count * shape_count - 1u);
                DispatchBarrier(cb);

                // CompactUnique compacts the sorted keys in place.
                compact_unique->Record(cb, *gpu_pair_keys, *gpu_unique_count, *gpu_pair_count, max_output_pair_count);
                DispatchBarrier(cb);

                uint32_t wg = (max_output_pair_count + 63u) / 64u;
                unpack_pairs_kernel->Dispatch(
                    cb,
                    {{"CompactedKeys", *gpu_pair_keys},
                     {"CollisionPairs", *gpu_collision_pairs},
                     {"UniqueCount", *gpu_unique_count},
                     {"PairCount", *gpu_pair_count}},
                    wg,
                    1,
                    1,
                    shape_count
                );
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

    BroadDetectorOutputBuffers SpatialHashBroadDetector::GetResultBuffers() const noexcept {
        return {
            .pair_buffer = m_impl->gpu_collision_pairs.get(),
            .pair_count_buffer = m_impl->gpu_pair_count.get(),
            .max_pairs = m_impl->max_output_pair_count
        };
    }

    void SpatialHashBroadDetector::BindToScene(
        PhysicsScene &scene,
        const GridConfig &grid_config,
        uint32_t fallback_all_pairs_threshold,
        uint32_t max_global_shape_count
    ) {
        const bool config_changed = m_impl->scene != &scene
                                    || m_impl->fallback_threshold != fallback_all_pairs_threshold
                                    || m_impl->max_global_shape_count != max_global_shape_count
                                    || m_impl->grid_config.cell_size != grid_config.cell_size
                                    || m_impl->grid_config.max_cells_per_shape != grid_config.max_cells_per_shape
                                    || m_impl->grid_config.world_min != grid_config.world_min
                                    || m_impl->grid_config.world_max != grid_config.world_max;

        m_impl->scene = &scene;
        m_impl->grid_config = grid_config;
        m_impl->fallback_threshold = fallback_all_pairs_threshold;
        m_impl->max_global_shape_count = max_global_shape_count;

        // A changed configuration invalidates the preparation the next Record
        // would otherwise reuse; the geometry itself is observed there.
        if (config_changed) {
            m_impl->prepared = false;
        }
    }

    void SpatialHashBroadDetector::Record(vk::CommandBuffer cb) {
        assert(m_impl->scene != nullptr && "BindToScene must be called before Record");

        // Prepare for the geometry the bound scene currently reports. Everything
        // that allocates, acquires a kernel or derives dispatch constants happens
        // here, before the call's first dispatch, and only when the observed shape
        // count changed since the previous preparation.
        {
            const uint32_t observed_shape_count = m_impl->scene->GetGpuBuffers().shape_slot_count;
            if (!m_impl->prepared || observed_shape_count != m_impl->shape_count) {
                m_impl->shape_count = observed_shape_count;
                m_impl->prepared = true;

                if (m_impl->shape_count > 0u) {
                    // The dedup encodes a candidate pair as the packed key
                    // `a * shape_count + b`.  Above this bound the packing wraps (and
                    // so does `shape_count * (shape_count - 1)` in the pair
                    // generators and in the buffer sizing below), which would
                    // silently produce a wrong dedup result.
                    assert(
                        m_impl->shape_count <= kMaxPackableShapeCount
                        && "shape_count exceeds the packed-pair key bound (65536)"
                    );

                    const GridConfig &grid_config = m_impl->grid_config;
                    const auto world_size = grid_config.world_max - grid_config.world_min;
                    m_impl->grid_dims = glm::ivec3{
                        glm::max(1, static_cast<int32_t>(glm::ceil(world_size.x / grid_config.cell_size))),
                        glm::max(1, static_cast<int32_t>(glm::ceil(world_size.y / grid_config.cell_size))),
                        glm::max(1, static_cast<int32_t>(glm::ceil(world_size.z / grid_config.cell_size)))
                    };
                    m_impl->grid_total_cells = static_cast<uint32_t>(m_impl->grid_dims.x)
                                               * static_cast<uint32_t>(m_impl->grid_dims.y)
                                               * static_cast<uint32_t>(m_impl->grid_dims.z);

                    const uint32_t cell_capacity = std::max(1u, m_impl->shape_count * grid_config.max_cells_per_shape);
                    m_impl->max_cell_shape_pair_count =
                        std::min(cell_capacity, 1u << 20); // cap at 1M to stay within RadixSort limits
                    m_impl->max_output_pair_count =
                        std::max(1u, std::min(m_impl->shape_count * (m_impl->shape_count - 1u) / 2u, 1u << 20));

                    m_impl->EnsureAllBuffers();
                    m_impl->EnsureKernels();

                    m_impl->grid_push.world_min_cell_size = glm::vec4(grid_config.world_min, grid_config.cell_size);
                    m_impl->grid_push.grid_dims_max_cells =
                        glm::ivec4(m_impl->grid_dims, static_cast<int32_t>(grid_config.max_cells_per_shape));
                    m_impl->grid_push.shape_slot_count = m_impl->shape_count;

                    if (!m_impl->scan) {
                        m_impl->scan = std::make_unique<ParallelScan>(m_impl->device_context);
                    }
                    if (!m_impl->radix_sort) {
                        m_impl->radix_sort = std::make_unique<RadixSort>(m_impl->device_context);
                    }
                    if (!m_impl->compact_unique) {
                        m_impl->compact_unique = std::make_unique<CompactUnique>(m_impl->device_context);
                    }
                }
            }
        }

        const auto gpu = m_impl->scene->GetGpuBuffers();

        if (gpu.shape_alive == nullptr || gpu.shape_world_position == nullptr || m_impl->shape_count <= 1u) {
            return;
        }

        // Both paths start the same way: publish the global-shape list, then compute
        // every shape's AABB (and its global flag). The path then continues with
        // either the all-pairs fallback or the spatial hash.
        DispatchBarrier(cb);
        m_impl->DispatchClear(cb, *m_impl->gpu_global_count, 1u);
        DispatchBarrier(cb);
        m_impl->RecordAABBPass(cb);
        DispatchBarrier(cb);

        if (m_impl->shape_count <= m_impl->fallback_threshold) {
            m_impl->RecordFallbackPath(cb);
        } else {
            m_impl->RecordSpatialHashPath(cb);
        }
    }
} // namespace Engine
