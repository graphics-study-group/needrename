# physics-gpu-shaders

## MODIFIED Requirements

### Requirement: XPBDGpuSolver loads precompiled SPIR-V

`engine/Physics/XPBDGpuSolver.cpp` SHALL load all XPBD compute shaders by reading precompiled `.spv` files from disk and SHALL NOT invoke `ShaderCompiler::CompileGLSLtoSPV` for these shaders. The loaded `std::vector<uint32_t>` SHALL be passed to the compute pipeline creation path unchanged.

Loading SHALL occur lazily on the first record of a step, at the point in `GPUStep` where the corresponding pass is first recorded. On loading failure (file missing, empty, or size not a multiple of 4 bytes), the loader SHALL throw `std::runtime_error` whose message includes the absolute path attempted.

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
- `solver/XPBDSolver/clear_entry_values.comp.spv` (entry count from a bound buffer)
- `solver/XPBDSolver/clear_entry_values_push.comp.spv` (entry count from the push-constant block)
- `solver/common/model_matrix.comp.spv`
- `solver/XPBDSolver/accumulate_hinge_position.comp.spv`
- `solver/XPBDSolver/accumulate_fixed_position.comp.spv`
- `solver/XPBDSolver/clear_hinge_lagrange.comp.spv`
- `solver/XPBDSolver/clear_fixed_lagrange.comp.spv`

There is no permutation-inversion shader: the sorted `(key, slot)` pair array produced by the radix sort is the segmented reduction's level-0 input, so `invert_permutation.comp.spv` SHALL NOT be loaded and SHALL NOT exist.

The `step.comp` placeholder SHALL be a no-op.

The `SumByKey` reduce shader (`algorithm/sum_by_key.comp.spv`) SHALL be loaded by the `SumByKey` algorithm class rather than directly by the solver, matching how `RadixSort` loads its own shaders.

The counted entry-value clear SHALL be a separate shader from `clear_int_buffer.comp` rather than a mode of it: it clears `num_channels` channel planes with a stride taken from a push-constant capacity, whereas `clear_int_buffer.comp` clears a flat range whose length is a push constant. Following the solver's existing convention, the two jobs SHALL NOT be selected by a mode flag.

The counted clear SHALL exist in one form per count source, following the convention `copy_uint.comp` / `copy_uint_push.comp` already establish. Only the **contact** group's count is produced on the GPU in the same substep, so only that group's clear reads it from a bound buffer at execution time; its dispatch geometry follows the entry capacity and the count bounds how many elements are written, for the same reason as the segmented reduction's level 0. The **hinge** and **fixed** groups' counts are CPU-known, so their clear takes the count from its push-constant block and its dispatch geometry follows the count itself, and it SHALL declare no count binding. The header comment of the buffer-count form SHALL state the GPU-produced case rather than claiming that the count can never be a push constant.

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
- **THEN** all four joint shader SPIR-V files are loaded from the same directory as contact shaders
- **AND** a compute pipeline is created for each

#### Scenario: No permutation inversion shader is loaded

- **WHEN** the solver initializes its pipelines
- **THEN** `invert_permutation.comp.spv` is not loaded and no pipeline is created for it
- **AND** the accumulated value scratch is indexed by entry slot, not by sorted position

#### Scenario: Entry-pass shaders are loaded alongside the accumulate shaders

- **WHEN** the solver initializes its pipelines
- **THEN** `entries/contact_entries.comp.spv`, `entries/hinge_entries.comp.spv` and `entries/fixed_entries.comp.spv` are loaded from `solver/XPBDSolver/entries/`
- **AND** a compute pipeline is created for each

#### Scenario: Counted clear shader is loaded alongside the flat clear shader

- **WHEN** the solver initializes its pipelines
- **THEN** `clear_entry_values.comp.spv` and `clear_entry_values_push.comp.spv` are loaded from `solver/XPBDSolver/`
- **AND** a compute pipeline is created for each
- **AND** `clear_int_buffer.comp.spv` is still loaded separately for the flat clears

### Requirement: SpatialHashBroadDetector shader source layout

Broad-phase detector GLSL source files SHALL live under `engine/Physics/shader/collision/SpatialHashBroadDetector/`. The following shaders SHALL exist:

- `compute_aabbs.comp` — per-shape AABB computation and global-shape marking with compact global list appending
- `count_cells.comp` — first pass of two-pass cell assignment
- `fill_cells.comp` — second pass of two-pass cell assignment
- `histogram_cells.comp` — counting sort histogram pass
- `scatter_sort.comp` — counting sort scatter pass
- `generate_broad_pairs.comp` — within-cell upper-triangle pair generation with AABB overlap pruning and collision filter checking
- `generate_global_pairs.comp` — global-shape × all-shapes pair generation via 2D dispatch with AABB overlap pruning
- `generate_all_pairs_fallback.comp` — all-pairs fallback for small N with AABB overlap pruning
- `memset_uint.comp` — clears a uint buffer to zero, with the element count in its push-constant block (reused across passes)
- `copy_uint.comp` — copies a uint buffer reading its element count from a bound buffer, for GPU-written counts
- `copy_uint_push.comp` — copies a uint buffer reading its element count from its push-constant block, for CPU-known counts

The three pair-generation shaders (`generate_broad_pairs.comp`, `generate_global_pairs.comp`, `generate_all_pairs_fallback.comp`) SHALL each bind `AabbMin` and `AabbMax` as `readonly buffer` and SHALL perform a 3-axis separating-axis AABB overlap test before emitting any candidate pair. The `compute_aabbs.comp` shader (which produces these buffers) is unchanged.

#### Scenario: Broad-phase shaders compiled to SPIR-V

- **WHEN** the engine build completes
- **THEN** all 11 broad-phase shader SPIR-V files exist under `<ENGINE_PHYSICS_SPIRV_DIR>/collision/SpatialHashBroadDetector/`
- **AND** each was compiled from its corresponding `.comp` source

#### Scenario: Pair-generation shaders bind AABB buffers

- **WHEN** `generate_broad_pairs.comp`, `generate_global_pairs.comp`, or `generate_all_pairs_fallback.comp` is dispatched
- **THEN** each shader has `AabbMin` and `AabbMax` bound as `readonly buffer` at descriptor set 0
- **AND** the matching C++ pass declares both buffers as read so the required barrier is inserted before the dispatch

#### Scenario: Copy variant matches the count source

- **WHEN** a caller copies a uint buffer whose element count it knows on the CPU
- **THEN** it records `copy_uint_push.comp` and pushes the count
- **WHEN** a caller copies a uint buffer whose element count was produced by an earlier GPU pass
- **THEN** it records `copy_uint.comp` and binds the count buffer
