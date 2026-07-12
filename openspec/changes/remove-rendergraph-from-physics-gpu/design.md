## Context

Physics GPU modules use `RenderGraph` — a rendering pipeline framework — for compute dispatch orchestration. This was expedient when the modules were first built, but creates friction:

- Physics modules are buffer-only (no images, no render passes, no attachments)
- RenderGraph buffer barrier generation compiles `vk::BufferMemoryBarrier2` but degrades to a single global `vk::MemoryBarrier2` at execution time (buffer fields are null, loop breaks after first iteration)
- Execution is already fully serial — all passes run on the same `vk::CommandBuffer`
- The lazy RG build → compile → record pipeline adds 3 layers of indirection for what is essentially `cb.DispatchCompute()`

ADR-0005 records the decision to remove RenderGraph from all physics GPU code.

## Goals / Non-Goals

**Goals:**
- Remove all `RenderGraph` / `RenderGraphBuilder` / `RenderGraphPass` dependencies from physics modules
- Replace with direct `Record(vk::CommandBuffer cb)` methods that dispatch compute and insert manual `vk::MemoryBarrier2`
- Pre-allocate shader pipelines, resource bindings, and data buffers in `Configure` / `PreGPUStep`
- Remove `GpuStateSnapshot` RG rebuild tracking from `XpbdGpuSolver`

**Non-Goals:**
- Changing shader dispatch logic or physics behavior
- Removing `RenderGraph` from rendering pipelines (editor, complex render graph)
- Changing `ISolver` interface or `PhysicsSystem`
- Introducing multi-queue compute submission

## Decisions

### 1. Barrier strategy: `MemoryBarrier2` at module entry + between internal passes

Every `Record(cb)` method inserts at its start:
```cpp
vk::MemoryBarrier2 barrier{
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
};
cb.PipelineBarrier2({}, barrier, {}, {});
```

Algorithm modules (ParallelScan, RadixSort, CompactUnique) self-manage barriers between their internal passes. Calling code just calls `Record(cb)` in sequence — each module's entry barrier ensures previous writes are visible.

**Rationale**: All dispatches are on the same queue, same pipeline stage. A single `MemoryBarrier2` per module boundary is sufficient. No per-buffer precision is needed.

### 2. Record(cb) pattern for all modules

Each module gets a `Record(vk::CommandBuffer cb)` method:

- **XpbdGpuSolver**: Single `Record(cb)` replaces 6 RG build + RecordAllPasses calls. Internally dispatches solver passes, calls `broad_detector->Record(cb)`, `narrow_detector->Record(cb)`, etc.
- **Detectors**: `Record(cb)` replaces `Detect(cb)` which previously returned result structs. Output buffers accessed via `GetResultBuffers()`.
- **Algorithms**: `Record(cb, buffers...)` replaces `AddPasses(RenderGraphBuilder&, buffers...)`. Buffers remain caller-provided parameters (algorithms own only ComputeStage and param buffer pools).

### 3. Binding pre-allocation in Configure/PreGPUStep

All `ComputeResourceBinding` allocation moves from hot path to setup phase:

- `XpbdGpuSolver::PreGPUStep` allocates bindings for all shader stages after `EnsureIntermediateBuffers`
- `SpatialHashBroadDetector::Configure` allocates bindings after `EnsureAllBuffers`
- `ConvexCollisionDetector::Configure` allocates bindings after `EnsureBuffers`
- `RadixSort::EnsureInitialized` allocates bindings for histogram, prefix_sum, scatter, and memset stages
- `ParallelScan::EnsureInitialized` allocates bindings for scan and offset stages
- `CompactUnique::EnsureInitialized` allocates bindings for flag and scatter stages

Bindings are rebuilt when buffer sizes change (VkBuffer handles become stale).

### 4. SpatialHash path selection: if-else in Record

Instead of building different RGs for fallback vs spatial hash, `SpatialHashBroadDetector::Record(cb)` uses an if-else branch. The threshold is cached from `Configure` and checked each frame. No rebuild detection needed.

### 5. Remove GpuStateSnapshot

`XpbdGpuSolver` tracked body_count/joint_count/shape_count changes to trigger RG rebuilds. With direct dispatch, buffer resizing is handled by `EnsureIntermediateBuffers` (already in PreGPUStep), and there's nothing to "rebuild" — Record always dispatches with current workgroup counts.

## Risks / Trade-offs

- **[Risk] Missing a barrier** → GPU data race. **Mitigation**: Entry barrier convention means every module call boundary is safe. Internal barriers within algorithms are explicit and reviewed.
- **[Risk] Binding lifecycle** → Stale VkBuffer handles after buffer resize. **Mitigation**: Binding rebuild triggered by buffer size change detection, same pattern as current buffer resize.
- **[Trade-off] Lost automatic barrier generation** → More manual code, but explicit and debuggable. The current auto-generation was broken for buffers anyway.
