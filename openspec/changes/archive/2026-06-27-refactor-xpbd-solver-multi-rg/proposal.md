## Why

The current `XPBDGpuSolver` uses an outdated architecture where a monolithic `AddStepPasses(RenderGraphBuilder &builder, ...)` method populates a shared RenderGraph with nested loops — substep loops, position iteration loops, and velocity iteration loops all live inside the builder, producing a single massive RG. Meanwhile, `DummySolver` has established a new pattern: solvers inherit `ISolver`, own their own `RenderGraph`, build it lazily in `GPUStep`, and do per-frame CPU preparation in `PreGPUStep`. The XPBD solver must be refactored to this new paradigm, further extended to use **multiple** RenderGraphs for its distinct phases, enabling clean separation of concerns, independent RG rebuild conditions, and loop RGs that can be recorded multiple times without the builder knowing about CPU-side loop logic.

## What Changes

- **BREAKING**: `XPBDGpuSolver` is renamed/rewritten to inherit `ISolver` as `XpbdGpuSolver`, dropping the old `AddStepPasses()` method
- `XpbdGpuSolver` owns **5–6** RenderGraphs instead of one: PreCollision, PostCollision, PositionIter, PostPosition/VelocityIter, and ModelMatrix — each built lazily and recorded in `GPUStep`
- Collision detectors (`ConvexCollisionDetector`, `SpatialHashBroadDetector`) are refactored from `AddDetectPasses(builder, ...)` to a two-phase `Configure(...)` / `Detect(cb)` API, where `Configure` handles CPU-side prep (buffer sizing, uploads, caching input buffer ptrs) and `Detect` lazily builds and records its own RenderGraph
- **BREAKING**: Detectors no longer accept pre-imported `RGBufferHandle` parameters or `PhysicsSceneBufferHandles` — they import scene buffers into their own RG directly via raw `ComputeBuffer*`. Narrow-phase receives broad-phase pair buffer pointers in `Configure()`
- ParallelScan remains a utility (pass-adding function, not an RG owner) called during detector RG build
- BroadPhase fallback path is decided at RG build time, constructing different RG structures for spatial-hash vs. all-pairs
- Cross-RG synchronization is enforced via explicit `prev_access` on `ImportExternalResource`, with a documented conservative strategy for loop RGs

## Capabilities

### New Capabilities
- `xpbd-solver-multi-rg`: `XpbdGpuSolver` as an `ISolver` implementation owning multiple RenderGraphs, each representing a distinct physics phase, with lazy build, per-frame CPU upload in `PreGPUStep`, and pass recording in `GPUStep`
- `detector-configure-detect`: Refactored collision detector API with `Configure()` (CPU prep, buffer sizing, uploads) and `Detect(cb)` (lazy RG build + record), replacing the old `AddDetectPasses(builder, ...)` pattern

### Modified Capabilities
- `physics-solver-interface`: The XPBD solver now implements `ISolver`; the concrete `XpbdGpuSolver` class uses `m_bound_scene` to access PhysicsScene buffers
- `gpu-convex-collision-detection`: `ConvexCollisionDetector` drops `AddDetectPasses(builder, scene, handles)` in favor of `Configure(scene, max_pairs, margin, pair_buf, count_buf)` + `Detect(cb)`, owns its own RenderGraph, no longer accepts pre-imported handles, receives broad-phase pair buffer pointers via Configure
- `spatial-hash-broad-phase`: `SpatialHashBroadDetector` drops `AddDetectPasses(builder, scene, handles)` in favor of `Configure(scene, shape_count, grid_config, threshold)` + `Detect(cb)`, owns its own RenderGraph, no longer accepts pre-imported handles
- `xpbd-contact-solve`: The XPBD solver's pass structure is refactored from a monolithic builder-populated graph to multiple independently-recorded RGs with CPU-side loop control
- `physics-render-graph-handle-forwarding`: The `PhysicsSceneBufferHandles` struct and detector output handle passing are deprecated — detectors now manage their own RG imports internally, and output handles are replaced by raw buffer access + solver-level `ImportExternalResource`

## Impact

- **Affected code**: `XPBDGpuSolver.{h,cpp}` (rewrite as `XpbdGpuSolver`), `ConvexCollisionDetector.{h,cpp}`, `SpatialHashBroadDetector.{h,cpp}`, `ParallelScan.{h,cpp}` (minor — remains utility), `PhysicsScene.h` (`PhysicsSceneBufferHandles` may be removed), `DummySolver.{h,cpp}` (reuse `XpbdConfig`)
- **Affected specs**: `physics-solver-interface`, `gpu-convex-collision-detection`, `spatial-hash-broad-phase`, `xpbd-contact-solve`, `physics-render-graph-handle-forwarding`
- **Example/tests**: `physics_example/main.cpp` and `physics_registration_test.cpp` may need updates for the new `XpbdGpuSolver` ISolver registration
- **No new dependencies**: All changes are internal to the physics module and RenderGraph system
