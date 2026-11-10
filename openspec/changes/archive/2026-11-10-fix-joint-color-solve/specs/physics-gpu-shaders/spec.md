## MODIFIED Requirements

### Requirement: XPBDGpuSolver loads precompiled SPIR-V

`engine/Physics/XPBDGpuSolver.cpp` SHALL load all XPBD compute shaders by reading precompiled `.spv` files from disk and SHALL NOT invoke `ShaderCompiler::CompileGLSLtoSPV` for these shaders. The loaded `std::vector<uint32_t>` SHALL be passed to the compute pipeline creation path unchanged.

Loading SHALL occur lazily on the first record of a step, at the point in `GPUStep` where the corresponding pass is first recorded. On loading failure (file missing, empty, or size not a multiple of 4 bytes), the loader SHALL throw `std::runtime_error` whose message includes the absolute path attempted.

The solver now loads the following shaders (replacing the single placeholder):
- `solver/XPBDSolver/integrate_forces.comp.spv`
- `solver/XPBDSolver/update_shape_world_pose.comp.spv`
- `solver/XPBDSolver/entries/contact_entries.comp.spv`
- `solver/XPBDSolver/accumulate_contact_position.comp.spv`
- `solver/XPBDSolver/apply_body_position_deltas.comp.spv`
- `solver/XPBDSolver/update_velocities_from_pose.comp.spv`
- `solver/XPBDSolver/accumulate_contact_velocity.comp.spv`
- `solver/XPBDSolver/apply_body_velocity_deltas.comp.spv`
- `solver/XPBDSolver/snapshot_position.comp.spv`
- `solver/XPBDSolver/clear_int_buffer.comp.spv`
- `solver/XPBDSolver/clear_entry_values.comp.spv` (entry count from a bound buffer)
- `solver/common/model_matrix.comp.spv`
- `solver/XPBDSolver/accumulate_hinge_position.comp.spv` (one hinge color, direct write)
- `solver/XPBDSolver/accumulate_fixed_position.comp.spv` (one fixed color, direct write)
- `solver/XPBDSolver/joint_color_sweep.comp.spv` (every joint color of a type in one workgroup)
- `solver/XPBDSolver/clear_hinge_lagrange.comp.spv`
- `solver/XPBDSolver/clear_fixed_lagrange.comp.spv`

There SHALL be no joint entry-pass shader: `entries/hinge_entries.comp.spv` and `entries/fixed_entries.comp.spv` SHALL NOT exist, because joints are solved from their descriptors through the color bucket index list rather than through a sorted entry list.

The counted entry-value clear SHALL exist in a single form, the bound-buffer count source used by the contact group. `clear_entry_values_push.comp.spv` SHALL NOT exist: no constraint type publishes a per-iteration entry scratch whose count is CPU-known.

There is no permutation-inversion shader: the sorted `(key, slot)` pair array produced by the radix sort is the segmented reduction's level-0 input, so `invert_permutation.comp.spv` SHALL NOT be loaded and SHALL NOT exist.

The `step.comp` placeholder SHALL be a no-op.

The `SumByKey` reduce shaders (`algorithm/sum_by_key.comp.spv` and its count-source variant `algorithm/sum_by_key_push.comp.spv`) SHALL be loaded by the `SumByKey` algorithm class rather than directly by the solver, matching how `RadixSort` loads its own shaders. The count source selects the variant: the buffer-count form reads a GPU-produced count from a bound buffer, and the value-count form takes a CPU-known count from its push-constant block.

The counted entry-value clear SHALL be a separate shader from `clear_int_buffer.comp` rather than a mode of it: it clears `num_channels` channel planes with a stride taken from a push-constant capacity, whereas `clear_int_buffer.comp` clears a flat range whose length is a push constant. Following the solver's existing convention, the two jobs SHALL NOT be selected by a mode flag.

The joint color passes SHALL share one implementation of the constraint math with the single-workgroup sweep: the per-joint hinge and fixed evaluation SHALL live in a shared GLSL include, so a joint evaluated by one color dispatch and the same joint evaluated inside the sweep compute the same correction.

#### Scenario: First Step call loads SPIR-V from disk

- **WHEN** the solver records its first `GPUStep` on a populated `PhysicsScene`
- **THEN** the solver reads its shader SPIR-V files from `<ENGINE_PHYSICS_SPIRV_DIR>`, the model matrix shader from `solver/common/` and the rest from `solver/XPBDSolver/`
- **AND** creates its compute pipelines from those words

#### Scenario: No GLSL compilation occurs at runtime for XPBD shaders

- **WHEN** the engine runs an XPBD physics example end-to-end
- **THEN** `ShaderCompiler::CompileGLSLtoSPV` is not invoked from `XPBDGpuSolver` code paths

#### Scenario: Missing SPIR-V file produces a diagnostic error

- **WHEN** any XPBD solver SPIR-V file does not exist at runtime
- **AND** the solver records a step
- **THEN** a `std::runtime_error` is thrown
- **AND** its `what()` includes the absolute path of the missing file

#### Scenario: Joint shader SPIR-V files are loaded alongside contact shaders

- **WHEN** the solver initializes its pipelines
- **THEN** the two per-color joint shaders, the single-workgroup joint color sweep shader and the two joint lagrange clear shaders are loaded from the same directory as the contact shaders
- **AND** a compute pipeline is created for each

#### Scenario: No permutation inversion shader is loaded

- **WHEN** the solver initializes its pipelines
- **THEN** `invert_permutation.comp.spv` is not loaded and no pipeline is created for it
- **AND** the accumulated value scratch is indexed by entry slot, not by sorted position

#### Scenario: Entry-pass shaders are loaded alongside the accumulate shaders

- **WHEN** the solver initializes its pipelines
- **THEN** `entries/contact_entries.comp.spv` is loaded from `solver/XPBDSolver/entries/`
- **AND** a compute pipeline is created for it
- **AND** no `entries/hinge_entries.comp.spv` or `entries/fixed_entries.comp.spv` is loaded or present, because joints have no entry pass

#### Scenario: Counted clear shader is loaded alongside the flat clear shader

- **WHEN** the solver initializes its pipelines
- **THEN** `clear_entry_values.comp.spv` is loaded from `solver/XPBDSolver/`
- **AND** a compute pipeline is created for it
- **AND** `clear_int_buffer.comp.spv` is still loaded separately for the flat clears
- **AND** `clear_entry_values_push.comp.spv` is neither loaded nor present

#### Scenario: Model matrix shader is loaded from the shared directory

- **WHEN** the solver needs the model matrix shader
- **THEN** it loads `solver/common/model_matrix.comp.spv` from `<ENGINE_PHYSICS_SPIRV_DIR>`
- **AND** no `solver/XPBDSolver/model_matrix.comp.spv` is loaded or produced

### Requirement: XPBD solver loads and dispatches multiple compute shaders

XPBDGpuSolver SHALL load, compile, and dispatch multiple compute shader passes per `Step()` call: force integration, shape world pose update, collision detection (broad and narrow phase via the detector classes), contact entry-list construction, radix sorting, accumulator clearing, contact position delta accumulation (scatter), per-body segmented reduction via `SumByKey`, body position delta application, the hinge and fixed joint color passes, velocity-from-pose update, contact velocity delta accumulation, body velocity delta application, buffer snapshot copies, and integer buffer clearing.

Each solver pass SHALL be a separate `.comp` file under `engine/Physics/shader/solver/XPBDSolver/` following the existing source layout convention. Algorithm shaders owned by `gpu_algorithm` classes (radix sort, `SumByKey`) SHALL live under `engine/Physics/shader/algorithm/`.

The joint color passes SHALL be the only joint constraint dispatches: there SHALL be no joint entry-list construction, no joint radix sort, no joint segmented reduction and no joint per-body apply pass.

#### Scenario: All XPBD shaders are loaded on first Step call

- **WHEN** `XPBDGpuSolver::Step` is called for the first time
- **THEN** the solver loads SPIR-V files for integrate forces, update shape world pose, contact entry-list construction, accumulate/apply position deltas, the joint color passes, update velocities, accumulate/apply velocity deltas, snapshot copy, clear int buffer, and the `SumByKey`/`RadixSort` algorithm modules
- **AND** a compute kernel exists for each

#### Scenario: Algorithm shaders load through their owning classes

- **WHEN** a reduction is recorded through the `SumByKey` class
- **THEN** the solver does not load `sum_by_key.comp.spv` itself
- **AND** the `SumByKey` class requests the kernel for its own module
