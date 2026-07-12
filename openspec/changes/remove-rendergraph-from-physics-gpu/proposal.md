## Why

Physics GPU modules (XPBD solver, collision detectors, GPU algorithm primitives) were built on `RenderGraph`, a resource-dependency graph designed for rendering pipelines — image attachments, layout transitions, and render pass wrapping. Physics modules only dispatch compute shaders on storage buffers. The `RenderGraph` buffer barrier support is incomplete (buffer barriers are downgraded to a single global `MemoryBarrier2`), execution is already fully serial, and the dependency on the full RenderGraph infrastructure adds unnecessary complexity with no benefit.

## What Changes

- Remove all `RenderGraph` / `RenderGraphBuilder` usage from physics GPU modules
- Replace lazy RG build + `RecordAllPasses` with direct `Record(vk::CommandBuffer cb)` methods
- Insert manual `vk::MemoryBarrier2` at module entry points and between internal passes
- Pre-allocate all shader pipelines, resource bindings, and data buffers in `Configure` / `PreGPUStep`
- Remove `GpuStateSnapshot` RG rebuild tracking from `XpbdGpuSolver`
- **BREAKING**: `Detect()` returns `void` instead of result structs (output buffers accessible via `GetResultBuffers()`)
- **BREAKING**: Algorithm modules (`ParallelScan`, `RadixSort`, `CompactUnique`) change from `AddPasses(RenderGraphBuilder&, ...)` to `Record(vk::CommandBuffer cb, ...)`

## Capabilities

### Modified Capabilities

- `xpbd-solver-multi-rg`: Remove RenderGraph multi-RG architecture; replace with single `Record()` method using manual barriers
- `detector-configure-detect`: `Detect()` returns `void` instead of result buffers; output accessed via `GetResultBuffers()`
- `physics-dummy-solver`: Remove RenderGraph usage; dispatch directly with manual barriers

## Impact

- Affected files: `XpbdGpuSolver.h/cpp`, `SpatialHashBroadDetector.h/cpp`, `ConvexCollisionDetector.h/cpp`, `DummySolver.h/cpp`, `ParallelScan.h/cpp`, `RadixSort.h/cpp`, `CompactUnique.h/cpp`
- Removed dependencies: `Render/Pipeline/RenderGraph/RenderGraph.h`, `RenderGraphBuilder.h`, `RenderGraphPass.h`, `RGAttachmentDesc.h`
- Retained dependencies: `ComputeStage`, `ComputeResourceBinding`, `ComputeBuffer`, `MemoryAccessHelper.hpp`, `CommandBuffer`
- `ISolver` interface and `PhysicsSystem` unchanged
- See ADR-0005 for design rationale
