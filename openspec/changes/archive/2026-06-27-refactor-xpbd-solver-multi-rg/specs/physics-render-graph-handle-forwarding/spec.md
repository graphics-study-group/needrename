# physics-render-graph-handle-forwarding

## REMOVED Requirements

### Requirement: PhysicsScene defines buffer handle struct alongside buffer pointer struct

**Reason**: `PhysicsSceneBufferHandles` is no longer needed. With the multi-RG architecture, each component (solver, detectors) owns its own RenderGraph and imports scene buffers independently via `builder.ImportExternalResource`. Handle forwarding across components was only needed when all passes shared a single builder. Cross-RG synchronization is now achieved through correct `prev_access` on `ImportExternalResource`.

**Migration**: Remove `PhysicsSceneBufferHandles` from `PhysicsScene.h`. Each component's RG build function calls `builder.ImportExternalResource(*buffer, prev_access)` directly for the buffers it needs.

### Requirement: Broad-phase detector returns output buffers and handles as bundled structs

**Reason**: `BroadDetectorOutputHandles` is no longer needed. With separate RGs, the solver imports detector-owned buffers into its own RG using `ImportExternalResource(*buf, prev_access)`. The detector's `Detect` method returns only `BroadDetectorOutputBuffers` (raw `ComputeBuffer*` references).

**Migration**: The solver calls `broad_bufs = broad_detector->Detect(cb)`, then imports `broad_bufs.pair_buffer` and `broad_bufs.pair_count_buffer` into its own RGs via `ImportExternalResource`.

### Requirement: Narrow-phase detector returns output buffers and handles as bundled structs

**Reason**: `NarrowDetectorOutputHandles` is no longer needed. Same reasoning as broad-phase — the solver imports detector-owned buffers directly.

**Migration**: The solver calls `collision_bufs = narrow_detector->Detect(cb)`, then imports the collision result buffers into PositionIterRG and VelocityIterRG via `ImportExternalResource`.

### Requirement: Detector AddDetectPasses() methods accept pre-imported scene buffer handles

**Reason**: Detectors no longer accept `PhysicsSceneBufferHandles` or pre-imported `RGBufferHandle` parameters. The `AddDetectPasses(builder, scene, handles)` method is replaced by `Configure(scene, ...)` + `Detect(cb)`. Detectors import scene buffers into their own RG internally.

**Migration**: Remove `PhysicsSceneBufferHandles` parameter from detector methods. Detectors cache `PhysicsScene*` from `Configure` and call `builder.ImportExternalResource` during `Detect`.

### Requirement: Handle forwarding applies within a single RenderGraph

**Reason**: This requirement assumed a single physics RenderGraph. With multi-RG, each component has its own RG and imports buffers independently.

**Migration**: No direct migration. The concept is replaced by cross-RG `prev_access` synchronization.

### Requirement: Cross-RenderGraph buffer sharing uses prev_access

**Reason**: This requirement described cross-RG synchronization between physics and rendering RGs. The pattern remains valid but is now generalized — ALL cross-RG synchronization (not just physics↔rendering) uses `prev_access`. This requirement is superseded by the multi-RG design.

**Migration**: The rendering RG continues to import `model_matrices` with `prev_access = ShaderRandomWrite`. Internal physics cross-RG synchronization follows the same pattern with appropriate `prev_access` values.

### Requirement: Solver consumes detector output handles without re-importing

**Reason**: With multi-RG, the solver MUST re-import detector-owned buffers into its own RGs. Each RG is independent and has its own handle namespace.

**Migration**: In `BuildPositionIterRG`, import detector output buffers via `builder.ImportExternalResource(*detector_output.collision_ids, {AT::ShaderRandomWrite})`.
