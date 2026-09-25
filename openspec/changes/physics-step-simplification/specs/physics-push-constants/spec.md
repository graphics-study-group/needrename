# physics-push-constants

## MODIFIED Requirements

### Requirement: Physics constant parameters are push constants

Each physics shader that consumes per-dispatch constants SHALL declare its own minimal `layout(push_constant)` block containing only the fields it uses, and the C++ side SHALL record the matching value immediately before each dispatch. The following parameter groups SHALL migrate from SSBOs to push constants: `XpbdUniforms` (gravity + substep dt), `DummySolverUniforms`, `DetectorConfig` (contact margin), `ShapeSlotCount`, `GridConfig`, the CPU-written count buffers (`body_count`, `contact_count`, `shape_slot_count`, hinge/fixed joint counts), `ScanParams`, `RadixSortParams`, and the constant `ElemCount` values (`256`, `1`, `grid_total_cells + 1`). GPU-written buffers (e.g. `TotalAssignments`, `PairCount`, `CollisionCount`, atomic counters) SHALL remain SSBOs. In particular, element counts that are produced by an earlier GPU pass and consumed later in the same command buffer (e.g. `PairCount` in `CompactUnique`) SHALL stay SSBO-bound, because their value is not known at record time; the push-constant `copy_uint_push.comp` SHALL be used only where the element count is a CPU-known value.

**The hinge and fixed entry counts are CPU-known and SHALL be push constants.** The host publishes `kJointSlotsPerJoint * joint_count` for each of those groups, and each group's entry capacity is `kJointSlotsPerJoint * max(1, joint_count)`, so the count equals the capacity once a joint exists and is zero when none does. It SHALL therefore travel in the push-constant block, and the host SHALL NOT write it into a host-visible buffer. Only the contact group's entry count, which an earlier entry pass produces on the GPU within the same substep, SHALL remain a bound buffer.

**A CPU-known count and a GPU-produced count SHALL be served by separate shaders rather than by a mode flag**, following the convention already established by `copy_uint.comp` (which keeps an SSBO `ElemCount` for GPU-written counts) and `copy_uint_push.comp` (which takes the count from the push block for CPU-known counts). The counted entry-value clear SHALL therefore exist in both forms: the SSBO-count form for the contact group and a push-count form for the hinge and fixed groups. Every form SHALL declare only the descriptor bindings it actually binds, because a declared-but-unbound binding is an error under the compute dispatch contract.

**The live body count SHALL be supplied explicitly and SHALL NOT be read from a buffer's length.** The three entry shaders (`contact_entries.comp`, `hinge_entries.comp`, `fixed_entries.comp`) currently derive their bound on the alive-body set from `rigid_body_alive.v.length()`, i.e. from the buffer's allocated size. Under the capacity contract that size is capacity, not the live slot count, so the derivation is wrong: it either overruns the logical set or is bounded by an unrelated allocation. Each of those shaders SHALL take the body count as a per-dispatch value (its push-constant block) and SHALL use it as its guard, and no physics shader SHALL use a buffer's length as a logical bound.

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

- **WHEN** the solver records a clear or a reduction for the hinge or fixed group
- **THEN** the group's entry count is part of the recorded push-constant value
- **AND** no host-visible count buffer is bound for that group
- **AND** no host-side write to GPU-visible memory occurs for it

#### Scenario: The counted clear has one form per count source

- **WHEN** the solver clears the hinge or fixed group's per-iteration scratch
- **THEN** it records the push-count form of the counted clear, which binds no count buffer
- **WHEN** the solver clears the contact group's per-iteration scratch
- **THEN** it records the SSBO-count form, which binds the contact entry-count buffer
- **AND** neither form declares a binding it does not bind

#### Scenario: Entry shaders take the body count as a value

- **WHEN** the solver records `contact_entries.comp`, `hinge_entries.comp` or `fixed_entries.comp`
- **THEN** the recorded push-constant block carries the live body count for that dispatch
- **AND** the shader guards its per-body work by that value
- **AND** it does not read `rigid_body_alive.v.length()` as the live body count

## ADDED Requirements

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

## REMOVED Requirements

### Requirement: No CPU writes to GPU memory during PreGPUStep

**Reason**: The requirement was scoped to `PreGPUStep`, which this change deletes. Its subject — the pre-recording phase in which a CPU write to a host-visible buffer was the convenient way to publish a parameter — no longer exists, and the two remaining CPU writes (`SetConstantU32` for the hinge and fixed entry counts) are eliminated rather than relocated, because both counts are CPU-known and now travel in the push-constant block. Leaving a phase-scoped prohibition in place would assert a rule about a method that is gone.

**Migration**: The rule is superseded by *No CPU writes to GPU-visible memory outside command-buffer recording*, which states the same intent unconditionally and without referring to a lifecycle phase. Code that published a parameter by writing a host-visible buffer moves the value into the push-constant block of the dispatch that consumes it; code that published a value produced on the GPU keeps using a bound buffer, which is not a CPU write.
