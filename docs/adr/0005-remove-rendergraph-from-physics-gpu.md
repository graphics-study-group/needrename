# Remove RenderGraph from Physics GPU Modules

Physics GPU modules (XPBD solver, broad/narrow phase detectors, GPU algorithm
primitives) were built on `RenderGraph`, a framework originally designed for
rendering pipelines with image attachments, layout transitions, and render pass
wrapping. Physics modules only dispatch compute shaders on storage buffers —
none of the image/render pass infrastructure is used.

The `RenderGraph` buffer barrier support is incomplete: barrier generation
compiles `vk::BufferMemoryBarrier2` with correct stage/access masks for each
buffer, but at execution time only a single global `vk::MemoryBarrier2` is
emitted (the buffer fields are never filled, and the per-buffer loop `break`s
after the first iteration).

**Decision**: Remove all `RenderGraph` usage from physics GPU modules.
Each module directly records compute dispatches to `vk::CommandBuffer` and
inserts manual `vk::MemoryBarrier2` barriers between passes. All dispatches
execute serially on the same command buffer — no cross-queue scheduling is
needed.

**Key design rules**:

- Every module entry point (`Record(cb)`) inserts a `MemoryBarrier2` at the
  start: `ComputeShader (ShaderStorageWrite) → ComputeShader (ShaderStorageRead | ShaderStorageWrite)`.
- Algorithm modules (ParallelScan, RadixSort, CompactUnique) self-manage
  barriers between internal passes.
- All shader pipelines, resource bindings, and data buffers are pre-allocated
  in Configure/PreGPUStep phases — Record() only dispatches.
- The `MemoryAccessHelper.hpp` dependency is retained for barrier access flag
  construction. `ComputeStage`, `ComputeResourceBinding`, and `ComputeBuffer`
  dependencies from the Render layer are retained — only the RenderGraph
  layer itself is removed.

**Trade-off**: Manual barriers trade RenderGraph's automatic dependency
analysis for explicit, predictable synchronization. Given the serial execution
model, the added control eliminates a broken abstraction without meaningful
cost.
