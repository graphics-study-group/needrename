# xpbd-contact-solve

Delta spec for the spatial-hash broad-phase change. The solver now runs a two-stage collision detection pipeline (broad-phase → narrow-phase) within each substep, with the pair buffer passed between stages.

## MODIFIED Requirements

### Requirement: Per-substep collision detection

The system SHALL run collision detection in two stages inside each substep loop, after force integration and shape world pose update have completed, and before position constraint solving begins:

1. **Broad-phase** (`SpatialHashBroadDetector::AddDetectPasses`): Computes per-shape AABBs, assigns shapes to spatial grid cells, sorts by cell ID, generates candidate collision pairs for shapes sharing at least one cell, applies collision filtering, and generates global-shape pairs.
2. **Narrow-phase** (`ConvexCollisionDetector::AddDetectPasses`): Reads the candidate pair buffer from broad-phase and runs MPR narrow-phase detection on each pair.

Collision detection SHALL be owned and managed internally by `XPBDGpuSolver` via `SpatialHashBroadDetector` and `ConvexCollisionDetector` instances created lazily at first `AddStepPasses()` call.

#### Scenario: Contacts detected via two-stage pipeline

- **WHEN** a box falls from z=5 to z=2 during force integration
- **AND** broad-phase runs after integration, identifying candidate pairs via spatial hash
- **AND** narrow-phase runs after broad-phase, running MPR on each candidate pair
- **THEN** the contact with the floor at z=0 is detected at z=2 (the new position)
- **AND** penetration depth reflects the current overlap

#### Scenario: Broad-phase narrow-phase integrated in solver

- **WHEN** `XPBDGpuSolver::AddStepPasses()` runs
- **THEN** `SpatialHashBroadDetector::AddDetectPasses(builder, physics_scene, scene_handles)` is called first
- **AND** its output handles (`BroadDetectorOutputHandles`) are retrieved
- **AND** `ConvexCollisionDetector::AddDetectPasses(builder, physics_scene, pair_buf, count_buf, scene_handles, pair_h, count_h)` is called second
- **AND** both stages run within each substep

### Requirement: Contact count guard with fixed dispatch

Per-constraint compute dispatches SHALL use a fixed workgroup count calculated from `max_collision_pairs` (the narrow-phase detector's buffer capacity). Threads with index >= `collision_count` SHALL return immediately.

The per-contact accumulation shaders (`accumulate_contact_position.comp`, `accumulate_contact_velocity.comp`) SHALL be sized for `max_contacts = max_pairs * 5` to accommodate up to 5 contact points per collision pair (4 perturbation + optionally 1 MPR fallback). The `max_pairs` value SHALL be the broad-phase detector's pair buffer capacity.

#### Scenario: Empty contact list dispatched correctly

- **WHEN** `collision_count` is 0
- **THEN** the dispatch still launches `(max_pairs * 5 + 63) / 64` workgroups
- **AND** all threads return immediately at the count guard

#### Scenario: Contact dispatch covers all 5 points per pair

- **WHEN** a collision pair produces 5 contact points (4 perturbation + MPR fallback)
- **AND** `max_pairs` is the broad-phase pair buffer capacity
- **THEN** the dispatch workgroup count is `(max_pairs * 5 + 63) / 64`
- **AND** all 5 contact points from that pair are within dispatch range
