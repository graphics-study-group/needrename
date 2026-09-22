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
- `solver/XPBDSolver/clear_entry_values.comp.spv`
- `solver/XPBDSolver/model_matrix.comp.spv`
- `solver/XPBDSolver/accumulate_hinge_position.comp.spv`
- `solver/XPBDSolver/accumulate_fixed_position.comp.spv`
- `solver/XPBDSolver/clear_hinge_lagrange.comp.spv`
- `solver/XPBDSolver/clear_fixed_lagrange.comp.spv`

There is no permutation-inversion shader: the sorted `(key, slot)` pair array produced by the radix sort is the segmented reduction's level-0 input, so `invert_permutation.comp.spv` SHALL NOT be loaded and SHALL NOT exist.

The `step.comp` placeholder SHALL be a no-op.

The `SumByKey` reduce shader (`algorithm/sum_by_key.comp.spv`) SHALL be loaded by the `SumByKey` algorithm class rather than directly by the solver, matching how `RadixSort` loads its own shaders.

`clear_entry_values.comp` SHALL be a separate shader from `clear_int_buffer.comp` rather than a mode of it: it reads the entry count from a buffer at execution time and clears `num_channels` channel planes with a stride taken from a push-constant capacity, whereas `clear_int_buffer.comp` clears a flat range whose length is a push constant. Following the solver's existing convention, the two jobs SHALL NOT be selected by a mode flag. Its dispatch geometry follows the capacity and the entry count only bounds how many elements it writes, for the same reason as the segmented reduction's level 0: the count is produced on the GPU in the same substep and is unknown when the command buffer is recorded.

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

#### Scenario: No permutation inversion shader is loaded

- **WHEN** `EnsureInitialized()` runs
- **THEN** `invert_permutation.comp.spv` is not loaded and no `ComputeStage` is created for it
- **AND** the accumulated value scratch is indexed by entry slot, not by sorted position

#### Scenario: Entry-pass shaders are loaded alongside the accumulate shaders

- **WHEN** `EnsureInitialized()` runs
- **THEN** `entries/contact_entries.comp.spv`, `entries/hinge_entries.comp.spv` and `entries/fixed_entries.comp.spv` are loaded from `solver/XPBDSolver/entries/`
- **AND** a `ComputeStage` instance is created for each

#### Scenario: Counted clear shader is loaded alongside the flat clear shader

- **WHEN** `EnsureInitialized()` runs
- **THEN** `clear_entry_values.comp.spv` is loaded from `solver/XPBDSolver/`
- **AND** a `ComputeStage` instance is created for it
- **AND** `clear_int_buffer.comp.spv` is still loaded separately for the flat clears
