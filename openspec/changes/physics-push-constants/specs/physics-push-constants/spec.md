# physics-push-constants

## Purpose

Remove render-frame concepts from physics internals: bindings drop to a single rotation slot, per-dispatch constant parameters move from CPU-written SSBOs and parameter pools to push constants recorded at command-buffer time, and the latent `DetectorConfig.contact_margin == 0` bug is fixed. Physics becomes a pure command-buffer recorder; command-buffer provisioning, submission and synchronization remain external responsibilities.

## Requirements

### Requirement: Physics bindings use a single rotation slot

All physics components (`XPBDGpuSolver`, `DummySolver`, `RadixSort`, `ParallelScan`, `CompactUnique`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`) SHALL allocate resource bindings with the default `slot_count = 1` and SHALL record dispatches through the slot-0 path only. No physics component SHALL maintain a frame counter, and `engine/Physics/` SHALL contain no literal `3` rotation depth or `% 3` frame-counter expression.

#### Scenario: No rotation literals remain in physics
- **WHEN** searching `engine/Physics/` for `AllocateResourceBinding(3)` and `m_frame_counter`
- **THEN** no matches are found

#### Scenario: XPBD dispatch paths are unified
- **WHEN** `XPBDGpuSolver::GPUStep` records a dispatch
- **THEN** every dispatch uses `Rhi::BindComputeResource(cb, stage, binding, 0)` (or the defaulted slot)
- **AND** no alternate `GetDescriptorSet`-based dispatch helper remains

### Requirement: Physics constant parameters are push constants

Each physics shader that consumes per-dispatch constants SHALL declare its own minimal `layout(push_constant)` block containing only the fields it uses, and the C++ side SHALL record the matching value immediately before each dispatch. The following parameter groups SHALL migrate from SSBOs to push constants: `XpbdUniforms` (gravity + substep dt), `DummySolverUniforms`, `DetectorConfig` (contact margin), `ShapeSlotCount`, `GridConfig`, the CPU-written count buffers (`body_count`, `contact_count`, `shape_slot_count`, hinge/fixed joint counts), `ScanParams`, `RadixSortParams`, and the constant `ElemCount` values (`256`, `1`, `grid_total_cells + 1`). GPU-written buffers (e.g. `TotalAssignments`, `PairCount`, `CollisionCount`, atomic counters) SHALL remain SSBOs. In particular, element counts that are produced by an earlier GPU pass and consumed later in the same command buffer (e.g. `PairCount` in `CompactUnique`) SHALL stay SSBO-bound, because their value is not known at record time; the push-constant `copy_uint_push.comp` SHALL be used only where the element count is a CPU-known value.

#### Scenario: XPBD integrate pass receives gravity and dt
- **WHEN** `XPBDGpuSolver::GPUStep` records the force-integration dispatch
- **THEN** the shader reads gravity and substep dt from its push-constant block
- **AND** no `XpbdUniforms` SSBO is bound to the stage

#### Scenario: Clear and copy passes receive element counts as push constants
- **WHEN** `SpatialHashBroadDetector` records a clear or copy pass
- **THEN** the element count (1 or `grid_total_cells + 1`) is pushed as a scalar
- **AND** `memset_uint.comp` / `copy_uint.comp` read it from their `{ uint elem_count; }` push-constant block

#### Scenario: Shared memset shader uses one push-constant contract
- **WHEN** both `SpatialHashBroadDetector` and `RadixSort` record the shared `memset_uint.comp` shader
- **THEN** both push the same `{ uint elem_count; }` block layout

#### Scenario: GPU-written counts stay as SSBOs
- **WHEN** a shader reads a buffer that is written by another GPU pass (e.g. `PairCount`, `CollisionCount`, `TotalAssignments`)
- **THEN** that buffer remains a descriptor-bound SSBO
- **AND** `CompactUnique`'s flag/scatter/copy passes bind the GPU-written `PairCount` buffer as `ElemCount` in the SSBO variant of the shaders

#### Scenario: Copy passes with CPU-known counts use the push-constant variant
- **WHEN** `SpatialHashBroadDetector` records a copy pass with a CPU-known element count (`grid_total_cells + 1` or `1`)
- **THEN** it records `copy_uint_push.comp` and pushes the count as a scalar
- **AND** the `copy_uint.comp` SSBO variant remains only for GPU-written-count callers

### Requirement: No CPU writes to GPU memory during PreGPUStep

After this change, `PreGPUStep` of each physics component SHALL NOT write to GPU-visible memory (`GetVMAddress()` writes). All values previously written there SHALL be stored in CPU members and recorded into the command buffer at `GPUStep` time.

#### Scenario: Parameter buffers are deleted
- **WHEN** searching `engine/Physics/` for the constant buffers (`gpu_uniforms`, `gpu_detector_config`, `gpu_grid_config`, `gpu_one`, `gpu_grid_cells_p1`, `gpu_const_256`, scan/radix parameter pools)
- **THEN** none of them are declared or written

### Requirement: Parameter pools are removed

`ParallelScan` and `RadixSort` SHALL NOT maintain parameter-buffer pools (`param_pool`, `histogram_param_pool`, `scatter_param_pool`, `Acquire*Param`, `ResetParamPool`). Per-dispatch parameter values SHALL be constructed locally at record time and pushed.

#### Scenario: Pool machinery is absent
- **WHEN** searching `engine/Physics/gpu_algorithm/` for `param_pool` and `ResetParamPool`
- **THEN** no matches are found

### Requirement: DetectorConfig contact margin is effective

The `contact_margin` configured for `ConvexCollisionDetector` SHALL reach the shader and be used by collision detection. Its value SHALL no longer be silently 0.

#### Scenario: Configured margin is observed by the detector shader
- **WHEN** a `ConvexCollisionDetector` is configured with `contact_margin = 0.001f` and the detect pass runs
- **THEN** `detect_collisions.comp` reads the configured margin from its push-constant block
- **AND** contact generation reflects the non-zero margin

### Requirement: C++ push layouts conform to shader declarations

For every physics push-constant parameter, the C++ side SHALL define a structure (or scalar) whose field order matches the shader's push-constant block and whose size equals the shader's declared block size — std430 member layout, with no struct-level 16-byte tail padding on the reflected size — guarded by `static_assert` on the expected size. `vec4`-family members SHALL precede scalar members so that C++ natural alignment matches the std430 member offsets.

#### Scenario: Layout drift is a compile-time failure
- **WHEN** a C++ push structure's size differs from its documented shader layout
- **THEN** the `static_assert` fails the build

#### Scenario: Mixed vec4 + scalar blocks use the declared size
- **WHEN** a shader declares `{ vec4 a; uint b; }` or `{ vec4 a; ivec4 b; uint c; }`
- **THEN** the reflected `push_constant_size` is 20 / 36 (no struct-level 16-byte padding)
- **AND** the C++ push structure is 20 / 36 bytes with `vec4` members first

### Requirement: Shader bindings are consecutive

After the constant SSBOs are removed, every physics shader SHALL declare its descriptor bindings consecutively from 0 to N without holes.

#### Scenario: Bindings are contiguous per shader
- **WHEN** inspecting any physics shader's `layout(set = 0, binding = ...)` declarations
- **THEN** the binding numbers are exactly `0, 1, ..., N` with no gaps
