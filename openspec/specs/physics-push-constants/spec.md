# physics-push-constants

## Purpose

Remove render-frame concepts from physics internals: bindings drop to a single rotation slot, per-dispatch constant parameters move from CPU-written SSBOs and parameter pools to push constants recorded at command-buffer time, and the latent `DetectorConfig.contact_margin == 0` bug is fixed. Physics becomes a pure command-buffer recorder; command-buffer provisioning, submission and synchronization remain external responsibilities.

## Requirements

### Requirement: Physics declares no rotation state

No physics component (`XPBDGpuSolver`, `DummySolver`, `RadixSort`, `ParallelScan`, `CompactUnique`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`) SHALL declare a rotation depth, select a rotation slot, or maintain a frame counter. Every physics dispatch SHALL be recorded through the compute kernel dispatch surface, which takes no slot parameter. `engine/Physics/` SHALL contain no literal `3` rotation depth and no `% 3` frame-counter expression.

#### Scenario: No rotation state remains in physics

- **WHEN** searching `engine/Physics/` for `AllocateResourceBinding`, `BindComputeResource` and `m_frame_counter`
- **THEN** no matches are found
- **AND** no standalone identifier denoting a descriptor-slot rotation depth or a rotation slot is declared by any physics component: a whole-identifier search for `slot_count` — matching that token alone, not the unrelated pre-existing `rigid_body_slot_count` and `shape_slot_count` — finds none

#### Scenario: XPBD dispatch paths are unified

- **WHEN** `XPBDGpuSolver::GPUStep` records a dispatch
- **THEN** every dispatch goes through the kernel dispatch surface
- **AND** no alternate descriptor-set-based dispatch helper remains

### Requirement: Physics constant parameters are push constants

Each physics shader that consumes per-dispatch constants SHALL declare its own minimal `layout(push_constant)` block containing only the fields it uses, and the C++ side SHALL record the matching value immediately before each dispatch. The following parameter groups SHALL migrate from SSBOs to push constants: `XpbdUniforms` (gravity + substep dt), `DummySolverUniforms`, `DetectorConfig` (contact margin), `ShapeSlotCount`, `GridConfig`, the CPU-written count buffers (`body_count`, `contact_count`, `shape_slot_count`, hinge/fixed joint counts), `ScanParams`, `RadixSortParams`, and the constant `ElemCount` values (`256`, `1`, `grid_total_cells + 1`). GPU-written buffers (e.g. `TotalAssignments`, `PairCount`, `CollisionCount`, atomic counters) SHALL remain SSBOs. In particular, element counts that are produced by an earlier GPU pass and consumed later in the same command buffer (e.g. `PairCount` in `CompactUnique`) SHALL stay SSBO-bound, because their value is not known at record time; the push-constant `copy_uint_push.comp` SHALL be used only where the element count is a CPU-known value.

**The joint color passes take their per-dispatch values from the push block.** Each per-color joint dispatch SHALL carry the number of joints entering that color, the color's start offset into the bucket index list, and the joint descriptor capacity as push-constant fields, so that one color's dispatch has no constant SSBO bound. The single-workgroup joint color sweep SHALL take the color count and the joint type's total joint count from its push block and SHALL read the per-color ranges from the color range buffer, because the number of colors is a property of the joint graph rather than a small fixed constant.

**Only the contact group has a per-iteration entry count, and it is produced on the GPU.** The contact entry pass publishes `min(2 * collision_count, entry_capacity)` into a device-local count buffer within the same substep, so that group's counted clear and its reduction read it from that buffer. No other constraint type has an entry list, an entry count, an entry count buffer or a count source: joints are solved from their descriptors through the color bucket list, which carries no per-iteration count. No host-side write to GPU-visible memory occurs for any count.

**A GPU-produced count and a CPU-known count SHALL be served by separate shaders rather than by a mode flag**, following the convention already established by `copy_uint.comp` (which keeps an SSBO `ElemCount` for GPU-written counts) and `copy_uint_push.comp` (which takes the count from the push block for CPU-known counts). Only the GPU-produced form of the counted entry-value clear exists, because the only per-iteration entry scratch left is the contact group's; the CPU-known form was removed together with the joint entry scratch. Every form SHALL declare only the descriptor bindings it actually binds, because a declared-but-unbound binding is an error under the compute dispatch contract.

**The live body count SHALL be supplied explicitly and SHALL NOT be read from a buffer's length.** The contact entry shader (`contact_entries.comp`) currently derives its bound on the alive-body set from `rigid_body_alive.v.length()`, i.e. from the buffer's allocated size. Under the capacity contract that size is capacity, not the live slot count, so the derivation is wrong: it either overruns the logical set or is bounded by an unrelated allocation. That shader SHALL take the body count as a per-dispatch value (its push-constant block) and SHALL use it as its guard, and no physics shader SHALL use a buffer's length as a logical bound. The entry shader SHALL therefore bind no body-count buffer at all: its per-body bound is a value, not a binding. `contact_entries.comp` SHALL likewise take the live shape slot count as a value rather than deriving it from `shape_bound_rigid_body.v.length()`.

#### Scenario: XPBD integrate pass receives gravity and dt
- **WHEN** `XPBDGpuSolver::GPUStep` records the force-integration dispatch
- **THEN** the shader reads gravity and substep dt from its push-constant block
- **AND** no `XpbdUniforms` SSBO is bound to the stage

#### Scenario: Clear and copy passes receive element counts as push constants
- **WHEN** `SpatialHashBroadDetector` records a clear or copy pass
- **THEN** the element count (1 or `grid_total_cells + 1`) is pushed as a scalar
- **AND** `memset_uint.comp` / `copy_uint_push.comp` read it from their `{ uint elem_count; }` push-constant block

#### Scenario: Shared memset shader uses one push-constant contract
- **WHEN** both `SpatialHashBroadDetector` and `RadixSort` record the shared `memset_uint.comp` shader
- **THEN** both push the same `{ uint elem_count; }` block layout

#### Scenario: GPU-written counts stay as SSBOs
- **WHEN** a shader reads a buffer that is written by another GPU pass (e.g. `PairCount`, `CollisionCount`, `TotalAssignments`, or the contact group's entry count)
- **THEN** that buffer remains a descriptor-bound SSBO
- **AND** `CompactUnique`'s flag/scatter/copy passes bind the GPU-written `PairCount` buffer as `ElemCount` in the SSBO variant of the shaders

#### Scenario: Copy passes with CPU-known counts use the push-constant variant
- **WHEN** `SpatialHashBroadDetector` records a copy pass with a CPU-known element count (`grid_total_cells + 1` or `1`)
- **THEN** it records `copy_uint_push.comp` and pushes the count as a scalar
- **AND** the `copy_uint.comp` SSBO variant remains only for GPU-written-count callers

#### Scenario: Joint entry counts travel in the push block

- **WHEN** the solver records a per-color joint dispatch
- **THEN** the number of joints entering that color travels in the push block, together with the color's bucket offset and the descriptor capacity
- **AND** no joint entry count, entry list or count buffer exists, because joints are solved straight from their descriptors through the color bucket list
- **AND** no host-visible count buffer is bound for the joint group

#### Scenario: The single-workgroup joint sweep reads its color ranges from a buffer

- **WHEN** the solver records the joint color sweep
- **THEN** the color count and the type's total joint count come from the push block
- **AND** the per-color ranges are read from the device-local color range buffer
- **AND** the push block's size does not depend on the number of colors

#### Scenario: The counted clear has one form per count source
- **WHEN** the solver clears the contact group's per-iteration scratch
- **THEN** it records the SSBO-count form, which binds the contact entry-count buffer
- **WHEN** the solver runs the joint color passes
- **THEN** no counted entry-value clear is recorded at all, because joints have no per-iteration entry scratch
- **AND** no push-count form of the counted clear exists

#### Scenario: Entry shaders take the body count as a value

- **WHEN** the solver records `contact_entries.comp`
- **THEN** the recorded push-constant block carries the live body count for that dispatch
- **AND** the shader guards its per-body work by that value
- **AND** it does not read `rigid_body_alive.v.length()` as the live body count

### Requirement: No CPU writes to GPU-visible memory outside command-buffer recording

No physics component SHALL write GPU-visible memory from the CPU at any point in the step lifecycle. Every value that reaches the GPU from the CPU SHALL either travel in a push-constant block recorded into the command buffer, or be written into a buffer through a recorded command. Buffers allocated with CPU access SHALL NOT be written by the CPU as a way to publish per-step parameters.

This replaces the former prohibition scoped to the pre-step phase: the requirement is now unconditional, because there is no longer any phase in which such a write is convenient.

#### Scenario: No host-visible parameter buffers remain

- **WHEN** searching `engine/Physics/` for host-visible constant buffers and the helper that wrote them (`gpu_uniforms`, `gpu_detector_config`, `gpu_grid_config`, `gpu_one`, `gpu_grid_cells_p1`, `gpu_const_256`, scan/radix parameter pools, `SetConstantU32`)
- **THEN** none of them are declared or written

#### Scenario: Parameters reach the GPU through the command buffer

- **WHEN** a physics parameter changes between steps
- **THEN** the new value is visible to the GPU without any CPU write to buffer memory
- **AND** it is carried either by a push-constant block or by a recorded command

#### Scenario: CPU-accessible buffers are not used for parameters

- **WHEN** a physics buffer is allocated with CPU access for readback purposes
- **THEN** no per-step parameter is published by writing it from the CPU

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
