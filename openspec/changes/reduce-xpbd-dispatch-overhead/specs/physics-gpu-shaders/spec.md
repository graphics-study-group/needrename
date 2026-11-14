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
- `solver/common/model_matrix.comp.spv`
- `solver/XPBDSolver/accumulate_hinge_position.comp.spv` (one hinge color, direct write)
- `solver/XPBDSolver/accumulate_fixed_position.comp.spv` (one fixed color, direct write)
- `solver/XPBDSolver/joint_color_sweep.comp.spv` (every fixed and hinge color in one workgroup)
- `solver/XPBDSolver/clear_hinge_lagrange.comp.spv`
- `solver/XPBDSolver/clear_fixed_lagrange.comp.spv`

There SHALL be no joint entry-pass shader: `entries/hinge_entries.comp.spv` and `entries/fixed_entries.comp.spv` SHALL NOT exist, because joints are solved from their descriptors through the color bucket index list rather than through a sorted entry list.

The standalone counted entry-value clear SHALL be removed. `clear_entry_values.comp.spv` and `clear_entry_values_push.comp.spv` SHALL NOT be loaded or produced: contact position and velocity accumulation initialize their own entry slots.

There is no permutation-inversion shader: the sorted `(key, slot)` pair array produced by the radix sort is the segmented reduction's level-0 input, so `invert_permutation.comp.spv` SHALL NOT be loaded and SHALL NOT exist.

The `step.comp` placeholder SHALL be a no-op.

The `SumByKey` reduce shaders (`algorithm/sum_by_key.comp.spv` and its count-source variant `algorithm/sum_by_key_push.comp.spv`) SHALL be loaded by the `SumByKey` algorithm class rather than directly by the solver, matching how `RadixSort` loads its own shaders. The count source selects the variant: the buffer-count form reads a GPU-produced count from a bound buffer, and the value-count form takes a CPU-known count from its push-constant block.

The joint color passes SHALL share one implementation of the constraint math with the single-workgroup sweep: the per-joint hinge and fixed evaluation SHALL live in a shared GLSL include, so a joint evaluated by one color dispatch and the same joint evaluated inside the sweep compute the same correction.

#### Scenario: First Step call loads SPIR-V from disk

- **WHEN** the solver records its first `GPUStep` on a populated `PhysicsScene`
- **THEN** the solver reads its shader SPIR-V files from `<resolved physics SPIR-V root>`, the model matrix shader from `solver/common/` and the rest from `solver/XPBDSolver/`
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

- **WHEN** the solver initializes its pipelines and a WebGPU physics shader build completes
- **THEN** the former counted-clear loading path is absent and neither entry-value clear shader is produced
- **AND** both contact accumulate shaders have pipelines that initialize their own entry slots
- **AND** `clear_int_buffer.comp.spv` remains loaded separately for flat clears

#### Scenario: Model matrix shader is loaded from the shared directory

- **WHEN** the solver needs the model matrix shader
- **THEN** it loads `solver/common/model_matrix.comp.spv` from `<resolved physics SPIR-V root>`
- **AND** no `solver/XPBDSolver/model_matrix.comp.spv` is loaded or produced

## ADDED Requirements

### Requirement: Joint sweep artifacts satisfy browser synchronization and binding contracts

The physics shader build SHALL produce SPIR-V, WGSL, and interface-description artifacts for `joint_color_sweep.comp` through the normal per-source pipeline; it SHALL NOT remain in a deferred-source list. The generated WGSL SHALL express each inter-color transition with storage synchronization and workgroup execution synchronization, and Tint validation SHALL succeed without a handwritten WGSL replacement or generic SPIR-V barrier rewrite.

Every physics shader enabled for the browser SHALL declare at most 16 storage buffers. The joint sweep SHALL use shared color IDs and offsets bindings at slots 14 and 15 and SHALL NOT declare the four former per-type color bindings.

#### Scenario: Joint sweep builds for WebGPU

- **WHEN** a WebGPU physics shader target is built
- **THEN** `joint_color_sweep.comp.spv`, its WGSL, and its interface description are produced
- **AND** conversion and WGSL validation succeed

#### Scenario: Inter-color synchronization survives conversion

- **WHEN** the generated joint sweep WGSL is inspected
- **THEN** each fixed and hinge color transition contains storage synchronization followed by workgroup execution synchronization
- **AND** no unsupported `OpMemoryBarrier` conversion failure occurs

#### Scenario: Browser storage-binding audit passes

- **WHEN** the enabled physics shader interfaces are audited for the browser
- **THEN** each declares at most 16 storage buffers
- **AND** the joint sweep declares exactly two shared color metadata buffers at bindings 14 and 15
