# Proposal: Decouple Rhi from render frame rotation

## Why

`ComputeResourceBinding` was born in the render pipeline and hard-codes the render-only frames-in-flight concept: `BACK_BUFFERS = 3`, per-frame descriptor-set slots, tri-sliced UBO rotation, and a `backbuffer` API parameter. Since it now lives in `Rhi`, physics components are forced to fabricate their own frame counters (`m_frame_counter++ % 3` in 8 components) just to satisfy the API — a second source of truth that silently desyncs from the render fif (e.g. `ParallelScan` advances twice per frame). Rotation depth is a commitment made by the *caller* about its own submission cadence; `Rhi` must not hard-code it, and must not leak render-frame semantics.

## What Changes

- `ComputeResourceBinding` constructor takes `uint32_t slot_count = 1` — rotation depth is declared by the caller, no hard-coded 3. **BREAKING** (API contract change; existing callers must pass their depth explicitly).
- `UpdateGPUInfo(uint32_t backbuffer)` / `GetDescriptorSet(uint32_t backbuffer)` renamed to `slot` with neutral semantics; both assert `slot < slot_count`.
- Internal storage becomes depth-parameterized: `descriptor_sets` sized to 8 (with `assert(slot_count <= 8)`), `ubo_dirty` stays `std::bitset<8>`, UBO `IndexedBuffer` created with the actual `slot_count`.
- `ComputeStage::AllocateResourceBinding(uint32_t slot_count = 1)` forwards the depth to the binding.
- `ComputeHelpers::BindComputeResource` parameter renamed `frame_index` → `slot`; header comments drop "frame-in-flight" / "back-buffer count" language.
- Call-site adaptation (mechanical, no behavior change):
  - Physics components (8 components, 37 call sites): pass literal `3` (keeps current behavior; to be reworked by the future physics push-constants change).
  - Render/editor/tests (8 call sites): pass `FrameManager::FRAMES_IN_FLIGHT`.
- No shader, serialization, or physics logic changes in this change.

## Capabilities

### New Capabilities
- `rhi-compute-resource-binding`: `ComputeResourceBinding` rotation-slot parameterization — callers declare their own depth; the API exposes a neutral `slot` index with bounds assertion.

### Modified Capabilities
- none

## Impact

- `engine/Rhi/ComputeResourceBinding.h/.cpp`, `ComputeHelpers.h/.cpp`, `ComputeStage.h/.cpp`
- Call sites: physics solvers/algorithms/detectors (`XPBDGpuSolver`, `DummySolver`, `RadixSort`, `ParallelScan`, `CompactUnique`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`), render graph (`ComplexRenderGraphBuilder`, editor `EditorRenderGraphBuilder`), tests (`headless_compute`, `pbr`, `new_material`, `compute_shader`, `compute_buffer`)
- No change to shaders, JSON assets, or serialization formats.
- Follow-up change (out of scope): physics push-constants parameter passing, which removes the remaining physics-side counters and lets physics bindings drop to `slot_count = 1`.
