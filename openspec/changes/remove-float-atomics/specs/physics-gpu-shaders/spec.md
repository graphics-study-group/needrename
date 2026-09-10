# physics-gpu-shaders

## MODIFIED Requirements

### Requirement: XPBDGpuSolver loads precompiled SPIR-V

`engine/Physics/XPBDGpuSolver.cpp` SHALL load all XPBD compute shaders by reading precompiled `.spv` files from disk and SHALL NOT invoke `ShaderCompiler::CompileGLSLtoSPV` for these shaders. The loaded `std::vector<uint32_t>` SHALL be passed to `ComputeStage::Instantiate` unchanged.

Loading SHALL occur lazily on first call to `Step()` (preserving the existing `EnsureInitialized()` behaviour). On loading failure (file missing, empty, or size not a multiple of 4 bytes), the loader SHALL throw `std::runtime_error` whose message includes the absolute path attempted.

The solver now loads the following shaders (replacing the single placeholder):
- `solver/XPBDSolver/integrate_forces.comp.spv`
- `solver/XPBDSolver/update_shape_world_pose.comp.spv`
- `solver/XPBDSolver/entries/contact_entries.comp.spv`
- `solver/XPBDSolver/entries/hinge_entries.comp.spv`
- `solver/XPBDSolver/entries/fixed_entries.comp.spv`
- `solver/XPBDSolver/accumulate_contact_position.comp.spv`
- `solver/XPBDSolver/apply_body_position_deltas.comp.spv`
- `solver/XPBDSolver/update_velocities_from_pose.comp.spv`
- `solver/XPBDSolver/accumulate_contact_velocity.comp.spv`
- `solver/XPBDSolver/apply_body_velocity_deltas.comp.spv`
- `solver/XPBDSolver/snapshot_position.comp.spv`
- `solver/XPBDSolver/clear_int_buffer.comp.spv`
- `solver/XPBDSolver/model_matrix.comp.spv`
- `solver/XPBDSolver/accumulate_hinge_position.comp.spv`
- `solver/XPBDSolver/accumulate_fixed_position.comp.spv`
- `solver/XPBDSolver/clear_hinge_lagrange.comp.spv`
- `solver/XPBDSolver/clear_fixed_lagrange.comp.spv`
- `solver/XPBDSolver/invert_permutation.comp.spv`

The `step.comp` placeholder SHALL be a no-op.

The `SumByKey` reduce shader (`algorithm/sum_by_key.comp.spv`) SHALL be loaded by the `SumByKey` algorithm class rather than directly by the solver, matching how `RadixSort` loads its own shaders.

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

#### Scenario: Joint shader SPIR-V files are loaded alongside contact shaders

- **WHEN** `EnsureInitialized()` runs
- **THEN** all four joint shader SPIR-V files are loaded from the same directory as contact shaders
- **AND** `ComputeStage` instances are created for each

#### Scenario: Permutation inversion shader is loaded alongside solver shaders

- **WHEN** `EnsureInitialized()` runs
- **THEN** `invert_permutation.comp.spv` is loaded from `solver/XPBDSolver/`
- **AND** a `ComputeStage` instance is created for it

#### Scenario: Entry-pass shaders are loaded alongside the accumulate shaders

- **WHEN** `EnsureInitialized()` runs
- **THEN** `entries/contact_entries.comp.spv`, `entries/hinge_entries.comp.spv` and `entries/fixed_entries.comp.spv` are loaded from `solver/XPBDSolver/entries/`
- **AND** a `ComputeStage` instance is created for each

### Requirement: XPBD solver loads and dispatches multiple compute shaders

XPBDGpuSolver SHALL load, compile, and dispatch multiple compute shader passes per `Step()` call: force integration, shape world pose update, collision detection (broad and narrow phase via the detector classes), entry-list construction (one pass per constraint type), radix sorting and permutation inversion, accumulator clearing, contact position delta accumulation (scatter), per-body segmented reduction via `SumByKey`, body position delta application, velocity-from-pose update, contact velocity delta accumulation, body velocity delta application, buffer snapshot copies, and integer buffer clearing.

Each solver pass SHALL be a separate `.comp` file under `engine/Physics/shader/solver/XPBDSolver/` following the existing source layout convention. Algorithm shaders owned by `gpu_algorithm` classes (radix sort, `SumByKey`) SHALL live under `engine/Physics/shader/algorithm/`.

#### Scenario: All XPBD shaders are loaded on first Step call

- **WHEN** `XPBDGpuSolver::Step` is called for the first time
- **THEN** the solver loads SPIR-V files for integrate forces, update shape world pose, entry-list construction, accumulate/apply position deltas, update velocities, accumulate/apply velocity deltas, snapshot copy, clear int buffer, and permutation inversion
- **AND** instantiates a `ComputeStage` for each

#### Scenario: Algorithm shaders load through their owning classes

- **WHEN** a reduction is recorded through the `SumByKey` class
- **THEN** the solver does not load `sum_by_key.comp.spv` itself
- **AND** the `SumByKey` class instantiates its own `ComputeStage`
