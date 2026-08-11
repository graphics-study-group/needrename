# Proposal: Physics push constants

## Why

After `decouple-rhi-from-frame-rotation`, physics components still fabricate render-frame semantics to satisfy the `ComputeResourceBinding` API: 37 call sites pass literal `3`, 7 components maintain `m_frame_counter++ % 3`, and per-dispatch constant parameters are shipped through CPU-written SSBOs (`GetVMAddress()` writes in `PreGPUStep`) plus two acquire/reset parameter pools (`ParallelScan`, `RadixSort`). These parameters are small, per-dispatch values that never need rotation; the rotation exists only because the API once forced a frame index. Physics must stop using render-only GPU concepts (frame rotation, per-frame CPU writes) and become a pure "record into the caller-provided command buffer" consumer. Additionally, `DetectorConfig.contact_margin` is silently always 0 today because the reflected UBO path overrides the C++-bound buffer with an unwritten `IndexedBuffer` — migrating to push constants fixes this latent bug.

## What Changes

- **Rhi push-constant infrastructure** (new capability):
  - `SPLayout` gains `push_constant_size`; `SPLayout::Reflect` reflects push-constant blocks from SPIR-V via `get_declared_struct_size`.
  - `ComputeStage::CreatePipeline` declares a `VkPushConstantRange{eCompute, 0, size}` when the shader has a push-constant block; `ComputeStage::GetPushConstantSize()` exposes it.
  - New template helper `Rhi::PushConstants(cb, stage, const T&)` records push constants with a size assertion; `BindComputeResource`'s `slot` parameter gets default `0`.
- **Physics bindings drop rotation**: all 37 `AllocateResourceBinding(3)` become the default `slot_count = 1`; all 7 `m_frame_counter` members are removed; `XPBDGpuSolver`'s dual dispatch paths unify on `BindComputeResource(..., 0)`.
- **Constant SSBOs become push constants** (per-shader minimal block, one block per shader): `XpbdUniforms`/`DummySolverUniforms` (gravity + dt), `DetectorConfig` + `ShapeSlotCount`, `GridConfig` + `shape_slot_count`, all CPU-written count buffers (`body_count`, `contact_count`, `shape_slot_count`, joint counts), `ScanParams`, `RadixSortParams`, and the constant `ElemCount` buffers (`gpu_const_256`, `gpu_one`, `gpu_grid_cells_p1`). All 16 CPU-written constant buffers are deleted together with their `GetVMAddress()` write sites; `PreGPUStep` performs no GPU-memory writes anymore. Element counts produced by earlier GPU passes in the same command buffer (e.g. `PairCount`) stay SSBO-bound; a new `copy_uint_push.comp` serves CPU-known-count copies while `copy_uint.comp` keeps its SSBO variant for GPU-written counts.
- **Param pools removed**: `ParallelScan`/`RadixSort` `param_pool`/`Acquire*Param`/`ResetParamPool` mechanisms are deleted; per-dispatch values are pushed at record time.
- **DetectorConfig fix** (expected behavior change): `contact_margin` becomes effective instead of the current constant 0.
- **Shader bindings renumbered** (final step, gated): constant SSBO declarations removed leave binding holes; bindings are made consecutive 0..N after review.
- **Tests**: `shader_refl_test` gains a push-constant reflection case (embedded GLSL); `headless_compute_test` gains a push-constant dispatch case verifying recorded values round-trip.

## Capabilities

### New Capabilities
- `rhi-push-constants`: `Rhi` reflects shader push-constant blocks into `SPLayout`, declares the matching pipeline-layout range on `ComputeStage`, and provides a `PushConstants` recording helper — the foundation for any Rhi consumer to pass small per-dispatch parameters without descriptor-set rotation.
- `physics-push-constants`: physics solvers, detectors and GPU algorithms pass all small per-dispatch parameters via push constants at record time, use single-slot bindings, and hold no frame-rotation or parameter-pool state — physics is a pure command-buffer recorder with no render-frame concepts.

### Modified Capabilities
- none

## Impact

- `engine/Rhi/ShaderParameterLayout.h/.cpp`, `ComputeStage.h/.cpp`, `ComputeHelpers.h/.cpp`
- Physics C++ (7 components): `XPBDGpuSolver`, `DummySolver`, `RadixSort`, `ParallelScan`, `CompactUnique`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`
- Physics shaders (~17 `.comp` files under `engine/Physics/shader/`), rebuilt automatically by the existing `physics_shader` target; GPU-written SSBOs (e.g. `TotalAssignments`, `PairCount`, `CollisionCount`) are untouched
- Tests: `engine/Tests/shader_refl_test.cpp`, `test/headless_compute_test.cpp`
- No change to shaders' loading pipeline, serialization, or render-side behavior
- Follow-up (out of scope): physics-independent submission (own command buffer / sync); this change only removes render concepts from physics internals — command-buffer provisioning, submission and sync remain external responsibilities
