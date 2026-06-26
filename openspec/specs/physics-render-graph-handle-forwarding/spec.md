# Physics Render Graph Handle Forwarding

## Purpose

TBD — This spec defines how GPU physics collision detection integrates with the RenderGraph system by passing pre-imported buffer handles instead of re-importing buffers within detector passes, ensuring correct barrier insertion and dependency analysis.

## Requirements

### Requirement: PhysicsScene defines buffer handle struct alongside buffer pointer struct

`PhysicsScene.h` SHALL define a `PhysicsSceneBufferHandles` struct containing pre-imported `RGBufferHandle` fields for each shared GPU buffer that the solver and collision detectors access. The struct SHALL be defined alongside the existing `PhysicsGpuBuffers` pointer struct. `PhysicsScene` SHALL NOT store or use this struct — it is a type definition only.

`RGBufferHandle` SHALL be forward-declared via `enum class RGBufferHandle : int32_t;` without including `<RGAttachmentDesc.h>`, since the underlying type is fixed and scoped enums with fixed underlying types can be opaque-declared per C++11.

#### Scenario: Handle struct mirrors buffer struct

- **WHEN** a new shared GPU buffer is added to `PhysicsGpuBuffers`
- **THEN** a corresponding field SHALL be added to `PhysicsSceneBufferHandles` in the same commit

### Requirement: Broad-phase detector returns output buffers and handles as bundled structs

`SpatialHashBroadDetector` SHALL define `BroadDetectorOutputBuffers` (raw `ComputeBuffer` references + `max_pairs`) and `BroadDetectorOutputHandles` (pre-imported `RGBufferHandle` values). The three scattered getters (`GetPairBuffer`, `GetPairCountBuffer`, `GetMaxPairs`) SHALL be replaced by a single `GetOutputBuffers()` method returning `BroadDetectorOutputBuffers`.

#### Scenario: Output buffers struct replaces scattered getters

- **WHEN** the solver needs the broad-phase output buffers for shader binding
- **THEN** it SHALL call `GetOutputBuffers()` and access `result.pair_buffer`, `result.pair_count_buffer`, and `result.max_pairs`

#### Scenario: Output handles are populated during Step

- **WHEN** `SpatialHashBroadDetector::AddDetectPasses()` completes
- **THEN** the returned `BroadDetectorOutputHandles` SHALL contain valid `RGBufferHandle` values for `pair_buffer` and `pair_count`, each mapped to the detector-owned `gpu_collision_pairs` and `gpu_pair_count` buffers

### Requirement: Narrow-phase detector returns output buffers and handles as bundled structs

`ConvexCollisionDetector` SHALL define `NarrowDetectorOutputHandles` alongside the existing `CollisionResultBuffers`. The `AddDetectPasses()` method SHALL return `NarrowDetectorOutputHandles` containing pre-imported handles for all five collision result buffers.

#### Scenario: Output handles population

- **WHEN** `ConvexCollisionDetector::AddDetectPasses()` completes
- **THEN** the returned `NarrowDetectorOutputHandles` SHALL contain valid `RGBufferHandle` values for `collision_ids`, `collision_normals`, `contact_point_a`, `contact_point_b`, and `collision_count`

### Requirement: Detector AddDetectPasses() methods accept pre-imported scene buffer handles

`SpatialHashBroadDetector::AddDetectPasses()` and `ConvexCollisionDetector::AddDetectPasses()` SHALL accept `const PhysicsSceneBufferHandles &` instead of calling `builder.ImportExternalResource` for scene buffers internally. Each detector SHALL use the provided handles when declaring buffer accesses via `RenderGraphPassBuilder::UseBuffer`.

Additionally, `ConvexCollisionDetector::AddDetectPasses()` SHALL accept `RGBufferHandle pair_buffer_handle` and `RGBufferHandle pair_count_handle` for the broad-phase output buffers, instead of re-importing them.

#### Scenario: All scene buffers use the same handle

- **WHEN** the solver calls `builder.ImportExternalResource(*gpu.shape_world_position)` to obtain handle H
- **AND** passes H via `PhysicsSceneBufferHandles` to both detectors
- **THEN** both detectors SHALL declare their passes with handle H for `shape_world_position`
- **THEN** the RenderGraph dependency analysis SHALL insert correct barriers between the solver's write passes and the detectors' read passes

#### Scenario: Pair buffer handle forwarded without re-import

- **WHEN** the broad detector imports its `gpu_collision_pairs` buffer as handle P during `AddDetectPasses()`
- **AND** the solver passes P as `pair_buffer_handle` to `ConvexCollisionDetector::AddDetectPasses()`
- **THEN** the narrow detector SHALL use P directly in `UseBuffer` without calling `builder.ImportExternalResource` for the same buffer

### Requirement: Handle forwarding applies within a single RenderGraph

The handle forwarding pattern defined in this spec (solver imports scene buffers once, passes handles to detectors) SHALL apply within a single physics `RenderGraph`. When a rendering `RenderGraph` shares buffers with the physics `RenderGraph`, it SHALL import those buffers independently using `ImportExternalResource` with `prev_access` reflecting the state left by the physics `RenderGraph` (see `physics-render-graph-separation` spec).

#### Scenario: Within-physics handle forwarding unchanged

- **WHEN** `XPBDGpuSolver::AddStepPasses()` imports scene buffers and passes handles to detectors
- **THEN** the behavior SHALL be identical to before this change — same handles used across solver and detector passes within the physics RenderGraph

### Requirement: Cross-RenderGraph buffer sharing uses prev_access

When a buffer is shared between the physics RenderGraph and the rendering RenderGraph, the rendering RenderGraph SHALL import the buffer independently (with its own `ImportExternalResource` call) and SHALL set `prev_access` to the access type the physics RenderGraph leaves the buffer with. This SHALL be `ShaderRandomWrite` for output buffers like `model_matrices`.

The physics RenderGraph SHALL NOT expose its internal `RGBufferHandle` values to the rendering RenderGraph. The two graphs SHALL be fully independent at the handle level, synchronized only through the shared `vk::CommandBuffer` recording order and correct `prev_access` declarations.

#### Scenario: Independent imports for shared buffer

- **WHEN** `model_matrices` is written by the physics RG and read by the rendering RG
- **THEN** both RGs SHALL call `ImportExternalResource` on the same `ComputeBuffer` instance
- **AND** the two imports SHALL return different `RGBufferHandle` values (one per builder)
- **AND** the rendering RG's import SHALL use `prev_access = ShaderRandomWrite`
- **AND** the physics RG's import SHALL use `prev_access = None`

### Requirement: Solver consumes detector output handles without re-importing

`XPBDGpuSolver::AddStepPasses()` SHALL use the `RGBufferHandle` values from `BroadDetectorOutputHandles` and `NarrowDetectorOutputHandles` directly in downstream passes (accumulate contact position, accumulate contact velocity, apply deltas). The solver SHALL NOT call `builder.ImportExternalResource` on buffers already imported inside the detectors.

#### Scenario: Collision results flow without duplicate import

- **WHEN** the narrow detector returns `narrow_out.collision_ids` handle C
- **THEN** the solver SHALL pass C directly to `UseBuffer` in the "XPBD Accum Contact Pos" pass
- **THEN** the RenderGraph dependency analysis SHALL insert a barrier between the narrow detector's write to that buffer and the solver's read

`XPBDGpuSolver::AddStepPasses()` SHALL use the `RGBufferHandle` values from `BroadDetectorOutputHandles` and `NarrowDetectorOutputHandles` directly in downstream passes (accumulate contact position, accumulate contact velocity, apply deltas). The solver SHALL NOT call `builder.ImportExternalResource` on buffers already imported inside the detectors.

#### Scenario: Collision results flow without duplicate import

- **WHEN** the narrow detector returns `narrow_out.collision_ids` handle C
- **THEN** the solver SHALL pass C directly to `UseBuffer` in the "XPBD Accum Contact Pos" pass
- **THEN** the RenderGraph dependency analysis SHALL insert a barrier between the narrow detector's write to that buffer and the solver's read
