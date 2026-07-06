## Why

The XPBD GPU solver, spatial-hash broad-phase detector, and convex narrow-phase detector each independently call `RenderGraphBuilder::ImportExternalResource` on the same `DeviceBuffer` instances (e.g., `shape_world_position`, `shape_alive`). Each call produces a distinct `RGBufferHandle` for the same physical buffer, causing the RenderGraph's dependency analysis to treat them as independent resources. This results in missing synchronization barriers and incorrect pass execution order observable in RenderDoc — passes execute as AABB → Convex → ClearInt → UpdateShape → FallbackPairs instead of the intended UpdateShape → AABB → Convex → ClearInt sequence.

## What Changes

- Add `PhysicsSceneBufferHandles` struct to `PhysicsScene.h` (forward-declared `RGBufferHandle` only — zero RenderGraph dependency) alongside the existing `PhysicsGpuBuffers` pointer struct
- Rename `XPBDGpuSolver::Step()` → `AddStepPasses()`, `SpatialHashBroadDetector::Step()` → `AddDetectPasses()`, `ConvexCollisionDetector::Step()` → `AddDetectPasses()` to clarify these methods build RenderGraph passes once at graph construction time, not per-frame — **BREAKING** API change
- Add `BroadDetectorOutputBuffers` and `BroadDetectorOutputHandles` structs to `SpatialHashBroadDetector.h`, replacing the three scattered getters (`GetPairBuffer`, `GetPairCountBuffer`, `GetMaxPairs`) with a single `GetOutputBuffers()` returning a bundled struct — **BREAKING** API change for `SpatialHashBroadDetector`
- Add `NarrowDetectorOutputHandles` struct to `ConvexCollisionDetector.h` alongside the existing `CollisionResultBuffers` struct
- Modify `SpatialHashBroadDetector::AddDetectPasses()` to accept `const PhysicsSceneBufferHandles&` and return `BroadDetectorOutputHandles`
- Modify `ConvexCollisionDetector::AddDetectPasses()` to accept `const PhysicsSceneBufferHandles&` plus pre-imported pair buffer handles, and return `NarrowDetectorOutputHandles`
- Update `XPBDGpuSolver::AddStepPasses()` to pre-import shape type/feature/filter buffers into `PhysicsSceneBufferHandles`, pass handles to both detectors, and consume returned output handles directly (eliminating duplicate `ImportExternalResource` calls for collision results)

## Capabilities

### New Capabilities
- `physics-render-graph-handle-forwarding`: Structured handle structs that carry pre-imported `RGBufferHandle` values from the solver to collision detectors and back, ensuring each GPU buffer maps to exactly one handle per frame

### Modified Capabilities
None. Existing functional behavior (collision detection, XPBD solving) is unchanged. Only the RenderGraph dependency declaration mechanism is refactored.

## Impact

- **Affected files**: `PhysicsScene.h`, `SpatialHashBroadDetector.h/.cpp`, `ConvexCollisionDetector.h/.cpp`, `XPBDGpuSolver.cpp`
- **No shader changes** — compute pipeline bindings and dispatch remain identical
- **No RenderGraph changes** — `ImportExternalResource` semantics are unchanged
- **No PhysicsScene changes** — it only hosts the struct definition, never stores or uses it
- **Caller impact**: `SpatialHashBroadDetector` getters consolidated into `GetOutputBuffers()`; `PhysicsExampleRenderGraphBuilder` updated from `Step()` to `AddStepPasses()` call; method renames are **BREAKING** but contained to physics module
