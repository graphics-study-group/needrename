# xpbd-contact-solve

## MODIFIED Requirements

### Requirement: Per-substep collision detection

The system SHALL run collision detection in two stages inside each substep loop, after force integration and shape world pose update have completed, and before position constraint solving begins:

1. **Broad-phase** (`SpatialHashBroadDetector::Detect(cb)`): Owns its own RenderGraph. Computes per-shape AABBs, assigns shapes to spatial grid cells, sorts by cell ID, generates candidate collision pairs. Called once per substep, records its RG directly to the command buffer.
2. **Narrow-phase** (`ConvexCollisionDetector::Detect(cb)`): Owns its own RenderGraph. Reads the candidate pair buffer from broad-phase and runs MPR narrow-phase detection. Called once per substep, records its RG directly to the command buffer.

Collision detection SHALL be owned and managed internally by `XpbdGpuSolver` via `SpatialHashBroadDetector` and `ConvexCollisionDetector` instances. Detector lifecycle SHALL follow the `Configure`/`Detect` two-phase pattern: `Configure` in `PreGPUStep`, `Detect` in `GPUStep`.

#### Scenario: Contacts detected via two-stage pipeline

- **WHEN** a box falls from z=5 to z=2 during force integration
- **AND** broad-phase runs after integration, identifying candidate pairs via spatial hash
- **AND** narrow-phase runs after broad-phase, running MPR on each candidate pair
- **THEN** the contact with the floor at z=0 is detected at z=2 (the new position)
- **AND** penetration depth reflects the current overlap

#### Scenario: Detectors record independent RGs in sequence

- **WHEN** `GPUStep(cb)` runs a substep iteration
- **THEN** `broad_detector->Detect(cb)` is called, recording its own RenderGraph to `cb`
- **AND** `narrow_detector->Detect(cb)` is called next, recording its own RenderGraph to `cb`
- **AND** the solver's PostCollisionPreIterRG is recorded after both detectors

### Requirement: Substep and iteration control

The system SHALL support configurable `num_substep_perstep`, `num_iter_persubstep`, and `num_velocity_iters` parameters. Each substep SHALL run force integration (recorded via PreCollisionRG), collision detection (via detector RGs), position solve iterations (via PositionIterRG recorded N times), velocity update (via PostPositionRG), and velocity solve iterations (via VelocityIterRG recorded M times), in that order.

The substep and iteration loops SHALL be expressed as CPU-side loops that call `RecordAllPasses(cb)` on pre-built RGs. Each RG SHALL be built once (lazily, on first frame or when parameters change) and re-recorded the appropriate number of times. Loop iteration counts (`substep_count`, `pos_iters`, `vel_iters`) SHALL NOT trigger RG rebuild.

#### Scenario: Loop RGs re-recorded without rebuild

- **WHEN** `pos_iters` is changed from 1 to 4 in `XpbdConfig`
- **THEN** PositionIterRG is NOT rebuilt
- **AND** `GPUStep` calls `PositionIterRG.RecordAllPasses(cb)` 4 times per substep instead of 1

### Requirement: Simulation toggle at dispatch time

Each solver compute pass (integration, shape world update, position/velocity solve) SHALL check `PhysicsScene::IsSimulationEnabled()` inside the dispatch lambda, evaluated at render graph execution time each frame. When simulation is paused, these passes SHALL skip dispatch. The model matrix update pass (ModelMatrixRG) SHALL always run to keep objects visible.

## ADDED Requirements

### Requirement: Multi-RG cross-synchronization via prev_access

The `XpbdGpuSolver` SHALL correctly set `prev_access` on every `ImportExternalResource` call in every RG build function, ensuring correct Vulkan pipeline barriers between independently-built RGs recorded in sequence.

For RGs recorded exactly once per phase (non-loop RGs), `prev_access` SHALL reflect the precise access state left by the preceding RG in the sequence. For RGs recorded multiple times in a loop (PositionIterRG, VelocityIterRG), `prev_access` for mutable buffers SHALL use the conservative `{AT::ShaderRandomRead, AT::ShaderRandomWrite}` to cover both the initial external state and the state left by the RG's own previous iteration.

#### Scenario: Loop RG uses conservative prev_access

- **WHEN** PositionIterRG is built
- **AND** it imports `rigid_body_center_world_position` (which it both reads and applies deltas to)
- **THEN** `ImportExternalResource` is called with `prev_access = {AT::ShaderRandomRead, AT::ShaderRandomWrite}`
- **AND** this ensures correct barriers for both the first iteration (after NarrowPhaseDetect leaves the buffer in RR state) and subsequent iterations (after the previous iteration left it in RW state)

#### Scenario: Non-loop RG uses precise prev_access

- **WHEN** PostCollisionPreIterRG is built and imports `rigid_body_center_world_position`
- **THEN** `ImportExternalResource` is called with `prev_access = {AT::ShaderRandomRead}` because the preceding NarrowPhaseDetect only read this buffer
