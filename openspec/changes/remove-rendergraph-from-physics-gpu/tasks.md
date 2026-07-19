## 1. GPU Algorithm Primitives — Remove RenderGraph

- [x] 1.1 ParallelScan: Replace `AddPasses(RenderGraphBuilder&, ...)` with `Record(vk::CommandBuffer cb, ComputeBuffer& input, ComputeBuffer& output, ComputeBuffer& block_sums, uint32_t elem_count)`. Move binding allocation from hot path to `EnsureInitialized`. Insert `MemoryBarrier2` between recursive scan passes.
- [x] 1.2 RadixSort: Replace `AddPasses(RenderGraphBuilder&, ...)` with `Record(vk::CommandBuffer cb, ComputeBuffer& pairs_a, ComputeBuffer& pairs_b, ComputeBuffer& scratch, uint32_t elem_capacity, ComputeBuffer& pair_count, uint32_t max_shape_count)`. Pre-allocate bindings for histogram, prefix_sum, scatter, and memset stages in `EnsureInitialized`. Insert barriers between clear→histogram→prefix_sum→scatter and between radix passes.
- [x] 1.3 CompactUnique: Replace `AddPasses(RenderGraphBuilder&, ...)` with `Record(vk::CommandBuffer cb, ComputeBuffer& pairs, ComputeBuffer& flags, ComputeBuffer& offsets, ComputeBuffer& count, ComputeBuffer& scan_scratch, ParallelScan& scan, ComputeBuffer& pair_count, uint32_t elem_capacity)`. Pre-allocate bindings. Insert barriers between flag→scan→scatter steps.

## 2. Collision Detectors — Remove RenderGraph

- [x] 2.1 ConvexCollisionDetector: Remove `BuildRenderGraph()`. Change `Detect(cb)` to `Record(cb)` returning `void`. Allocate all bindings in `Configure` after `EnsureBuffers`. Insert entry barrier at start of `Record`. Dispatch clear + detect passes directly.
- [x] 2.2 SpatialHashBroadDetector: Remove `BuildRenderGraph()`. Change `Detect(cb)` to `Record(cb)` returning `void`. Allocate all bindings (including memset/copy stages) in `Configure` after `EnsureAllBuffers`. Replace fallback vs spatial-hash RG selection with if-else in `Record`. Entry barrier at start. Call algorithm `Record` methods directly instead of `AddPasses`.

## 3. Solver Modules — Remove RenderGraph

- [x] 3.1 DummySolver: Remove `BuildRenderGraph()` and `m_rg`. Load shader and allocate binding in `PreGPUStep`. In `GPUStep`, insert entry barrier and dispatch compute directly via `cb.BindComputeStage`/`cb.DispatchCompute`.
- [x] 3.2 XpbdGpuSolver: Remove all 6 `Build*RG()` methods. Remove `GpuStateSnapshot` RG rebuild tracking. Allocate all bindings in `PreGPUStep` after `EnsureIntermediateBuffers`. Replace `GPUStep` body with direct dispatch sequence calling internal phases and `broad_detector->Record(cb)` / `narrow_detector->Record(cb)`. Insert entry barrier at start of each phase group.

## 4. Cleanup

- [x] 4.1 Remove RenderGraph includes from all affected files: `RenderGraph.h`, `RenderGraphBuilder.h`, `RenderGraphPass.h`, `RGAttachmentDesc.h`
- [x] 4.2 Remove `MemoryAccessTypes` aliases (`RR`, `RW`, `WW`, `None`) and related convenience code no longer needed
- [x] 4.3 Verify compilation with `cmake --build build` (MinGW Makefiles, Debug)
