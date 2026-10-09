## MODIFIED Requirements

### Requirement: Physics classes export PHYSICS_API

The classes `PhysicsScene`, `PhysicsSystem`, `ISolver`, `XpbdGpuSolver`, `DummySolver`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`, and the struct `PhysicsScene::PhysicsGpuBuffers` SHALL be declared with `PHYSICS_API` (defined in `engine/Physics/physics_export.h`, pattern of `core_export.h` / `asset_export.h`).

The two collision detectors SHALL be exported alongside the solver because they own state no exported solver API reaches: the published contact count and the contact capacity live in the detector's result buffers, so a test that must assert the count's bound cannot get at them through `XpbdGpuSolver`. Exporting them is what makes the detector's contract testable rather than only reviewable.

#### Scenario: Cross-DLL instantiation works

- **WHEN** `MainClass` (in `Engine.dll`) constructs an `XpbdGpuSolver` and registers it via `PhysicsSystem::RegisterSolver`
- **THEN** the type symbols resolve across the DLL boundary
- **AND** `PhysicsScene::PhysicsGpuBuffers` fields are readable from `Engine.dll`

#### Scenario: A collision detector is drivable from outside the module

- **WHEN** a test outside `EnginePhysics` constructs a `SpatialHashBroadDetector` and a `ConvexCollisionDetector`, binds them to a scene and records a detection pass
- **THEN** the constructor, `BindToScene`, `Record` and `GetResultBuffers` symbols resolve across the DLL boundary
- **AND** `ConvexCollisionDetector::GetResultBuffers()` yields the detector's result buffers, so the published `collision_count` is readable from the test
