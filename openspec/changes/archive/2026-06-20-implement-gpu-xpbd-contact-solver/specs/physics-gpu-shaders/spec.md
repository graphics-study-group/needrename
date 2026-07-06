## ADDED Requirements

### Requirement: XPBD solver loads and dispatches multiple compute shaders

XPBDGpuSolver SHALL load, compile, and dispatch multiple compute shader passes per `Step()` call: force integration, shape world pose update, contact position delta accumulation, body position delta application, velocity-from-pose update, contact velocity delta accumulation, body velocity delta application, buffer snapshot copies, and integer buffer clearing.

Each shader SHALL be a separate `.comp` file under `engine/Physics/shader/solver/XPBDSolver/` following the existing source layout convention.

#### Scenario: All XPBD shaders are loaded on first Step call

- **WHEN** `XPBDGpuSolver::Step` is called for the first time
- **THEN** the solver loads SPIR-V files for integrate forces, update shape world pose, accumulate/apply position deltas, update velocities, accumulate/apply velocity deltas, snapshot copy, and clear int buffer
- **AND** instantiates a `ComputeStage` for each

#### Scenario: Per-substep shape world update runs before collision detection

- **WHEN** the substep loop executes
- **THEN** the shape world pose update compute pass is declared after force integration and before collision detection
- **AND** it declares `UseBuffer` for rigid body position/rotation (read) and shape world position/rotation (write)
- **AND** the render graph ensures correct ordering via these buffer dependencies

### Requirement: Shape world pose update shader

The system SHALL provide `update_shape_world_pose.comp` under `engine/Physics/shader/solver/XPBDSolver/` that recomputes each shape's world-space position and rotation from its owning rigid body's current pose and the shape's local offset.

The shader SHALL read `ShapeAlive`, `ShapeBoundRigidBody`, `ShapeLocalPosition`, `ShapeLocalRotation`, `RigidBodyCenterPosition`, and `RigidBodyCenterRotation`. It SHALL write `ShapeWorldPosition` and `ShapeWorldRotation`. Dead shapes SHALL be skipped. Unbound shapes SHALL copy local pose to world directly.

### Requirement: Quaternion multiplication helper

The shared GLSL header `xpbd_math.glsl` SHALL provide a `quat_mul(vec4 a, vec4 b)` function for quaternion multiplication, used by `update_shape_world_pose.comp`.

## MODIFIED Requirements

### Requirement: XPBDGpuSolver loads precompiled SPIR-V

`engine/Physics/XPBDGpuSolver.cpp` SHALL load all XPBD compute shaders by reading precompiled `.spv` files from disk and SHALL NOT invoke `ShaderCompiler::CompileGLSLtoSPV` for these shaders. The loaded `std::vector<uint32_t>` SHALL be passed to `ComputeStage::Instantiate` unchanged.

Loading SHALL occur lazily on first call to `Step()`. On loading failure, the loader SHALL throw `std::runtime_error` whose message includes the absolute path attempted.

The solver now loads the following shaders:
- `solver/XPBDSolver/integrate_forces.comp.spv`
- `solver/XPBDSolver/update_shape_world_pose.comp.spv`
- `solver/XPBDSolver/accumulate_contact_position.comp.spv`
- `solver/XPBDSolver/apply_body_position_deltas.comp.spv`
- `solver/XPBDSolver/update_velocities_from_pose.comp.spv`
- `solver/XPBDSolver/accumulate_contact_velocity.comp.spv`
- `solver/XPBDSolver/apply_body_velocity_deltas.comp.spv`
- `solver/XPBDSolver/snapshot_position.comp.spv`
- `solver/XPBDSolver/clear_int_buffer.comp.spv`
- `solver/XPBDSolver/model_matrix.comp.spv`

The `step.comp` placeholder SHALL be a no-op.

#### Scenario: First Step call loads SPIR-V from disk

- **WHEN** `XPBDGpuSolver::Step` is called for the first time on a populated `PhysicsScene`
- **THEN** the solver reads all XPBD shader SPIR-V files from `<ENGINE_PHYSICS_SPIRV_DIR>/solver/XPBDSolver/`
- **AND** instantiates `ComputeStage` instances from those words

#### Scenario: No GLSL compilation occurs at runtime for XPBD shaders

- **WHEN** the engine runs an XPBD physics example end-to-end
- **THEN** `ShaderCompiler::CompileGLSLtoSPV` is not invoked from `XPBDGpuSolver` code paths

#### Scenario: Missing SPIR-V file produces a diagnostic error

- **WHEN** any XPBD solver SPIR-V file does not exist at runtime
- **AND** `XPBDGpuSolver::Step` is called
- **THEN** a `std::runtime_error` is thrown
- **AND** its `what()` includes the absolute path of the missing file

### Requirement: Collision detector owned and managed by solver

The `ConvexCollisionDetector` instance SHALL be owned by `XPBDGpuSolver::Impl`, not by external callers. It SHALL be created lazily in a dedicated `EnsureCollisionDetector` method that sizes the detector from `PhysicsScene::GetGpuBuffers().shape_slot_count`. If the shape count changes between frames, the detector SHALL be recreated.

`XPBDGpuSolver::Step()` SHALL NOT accept `CollisionResultBuffers` as a parameter. The solver SHALL obtain collision result buffers internally from its owned detector.

#### Scenario: Solver creates collision detector on first frame

- **WHEN** `XPBDGpuSolver::Step` is called for the first time with shapes present
- **THEN** the solver creates a `ConvexCollisionDetector` with `max_pairs = N*(N-1)/2`
- **AND** collision detection runs inside each substep

#### Scenario: External caller has no collision detector dependency

- **WHEN** an application uses `XPBDGpuSolver`
- **THEN** it does not need to create, own, or pass a `ConvexCollisionDetector`
- **AND** the `Step()` call is `Step(builder, physics_scene, mm_handle)`
