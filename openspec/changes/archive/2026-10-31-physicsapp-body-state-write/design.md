# Design: Body-state write semantics and episode reset

## Context

See proposal.md for motivation. State that shapes this design:

- `PhysicsApp::Impl` keeps two host-visible staging sets per body state: four
  readback buffers (`pos/rot/linvel/angvel_staging`, `ReadbackFromDevice`,
  refreshed by a whole-field GPU→CPU copy at the end of every `Step`) and six
  write buffers (`write_staging`, `StagingToDevice`, zero-seeded at
  `CommitScene`, uploaded whole-field per dirty field before `GPUStep`).
- `GetBodyState` / `GetBodyStates` / `GetJointState`, `RunActuators` and
  `SetJointTorque` read the readback buffers; `SetBodyValue` writes the write
  buffers. The two sets are never synchronized, which is the read-after-write
  gap and, together with whole-field upload from a zero-seeded buffer, the
  clobber bug for unwritten slots.
- `Step` is synchronous: it waits on a fence around a dedicated command buffer
  (`WaitForIdle` before and after), so between two `Step` calls the GPU touches
  none of these host buffers.
- `CommitScene` already performs a one-time seed copy (GPU→host) so reads are
  legal before the first `Step`; that copy is a natural place to snapshot the
  initial state.
- XPBD derives velocity from the pose change each substep
  (`update_velocities_from_pose.comp`), so a restored velocity is an
  integration seed, never a persistent state. The specs already say so.
- `Actuator::Reset()` (added by `physicsapp-actuator-api`) is the hook for
  clearing actuator internal state; actuator targets are plain data on the
  base class.
- Host memory types: `StagingToDevice` maps to VMA sequential-write,
  `ReadbackFromDevice` to random-access; `vmaFlushAllocation` /
  `vmaInvalidateAllocation` are no-ops on coherent memory types and real calls
  otherwise.

## Goals / Non-Goals

**Goals:**
- One coherent notion of "current body state": reads return simulated state
  overlaid with pending writes, and actuator measurement uses the same view.
- Whole-field upload stays correct for slots the caller never touched.
- Episode reset is one app-level call restoring the commit-time state,
  including actuator targets and actuator internal state.
- No per-call device synchronization; write-heavy loops stay CPU-cheap.

**Non-Goals:**
- Per-body / per-robot scoped reset (whole-app reset only; scoped restore can
  be added later without changing this interface).
- Arbitrary mid-episode save/restore ("time travel") snapshots.
- Making restored velocities authoritative against XPBD's pose-delta
  derivation (would need solver-level velocity holds).
- Asynchronous or multi-threaded stepping; `Step` stays synchronous.

## Decisions

### D1: One shared host buffer per readback-backed field ("simulated state + pending writes")

For `Position`, `Rotation`, `LinearVelocity`, `AngularVelocity`, merge the
readback and write staging into a single host buffer per field (buffer usage
gains `CopyFrom` so it is also a transfer source). Invariant:

> shared buffer ≡ last simulated state (step-end GPU→CPU copy) + pending
> writes (slots written since that copy).

`SetBodyValue` writes a slot in the shared buffer; reads return the shared
buffer; the pre-step upload copies the whole field for dirty fields; the
step-end readback copies the whole field back. Consequences: read-after-write
for free; uploading untouched slots is idempotent (they carry their own current
values), which fixes the clobber bug; buffer count halves.

Alternatives considered: (a) mirror writes into both buffers — fixes
read-after-write but not the clobber, since the write buffer still lacks
evolved state for untouched slots; (b) seed the write buffer from the readback
each step (host memcpy) — same fix, one extra buffer set and one more thing to
keep in sync; (c) per-slot dirty sets with compacted upload ranges — see D2.

### D2: Field-granularity dirty flags and whole-field upload (unchanged batching)

Dirty tracking stays `any_dirty[6]` (one bool per field, already exists): any
number of slot writes to a field sets one bool; `Step` records at most six
whole-field `copyBuffer` records. Upload volume is slot_count × 16 bytes per
dirty field (500 bodies ≈ 8 KB; all six fields ≈ 48 KB per step) — memcpy-scale
work next to the solver's per-step cost.

Per-slot dirty tracking with one `vkCmdCopyBuffer` carrying N ranges was
considered and rejected: it adds dirty-range bookkeeping to save tens of KB of
copies per step, and the interface would have to expose nothing new anyway. If
profiling ever shows the whole-field copy matters, it can be added internally.

### D3: Host synchronization at step boundaries only

`SetBodyValue` performs plain stores and sets the dirty flag — no Vulkan calls.
Before recording uploads, `Step` flushes each dirty field's range once
(`vmaFlushAllocation`; a no-op on coherent memory). After the step-end
readback, CPU reads are preceded by one `Invalidate` per field (defensive;
today the code reads without it and relies on coherent memory). This replaces
the current per-call `Flush` inside `SetBodyValue`, so a write loop costs one
store per call regardless of count.

### D4: `ResetToInitial()` = restore the commit-time snapshot for the whole app

`CommitScene`, after the seed copy, snapshots every slot's pose/velocity (four
`std::vector<glm::vec4>`) and each actuator's target angle into `Impl`.
`ResetToInitial()` (Drive phase) writes the pose/velocity slots back through
the shared buffers, zeroes the `ExternalForce`/`ExternalTorque` staging,
restores actuator targets and calls `Actuator::Reset()` on every actuator.
Reads see the restored state immediately (D1); the next `Step` uploads it.

Velocity is written as an integration seed, matching the spec language already
in place. The snapshot is per app, not per robot: one app per process and one
robot per app is the supported shape; scoped restore is a future extension
(`RestoreBodyStates` over a body subset) that reuses the same write path.

Alternatives considered: replaying per-body `SetBodyValue` from a Python
snapshot (current behavior — keeps the read gap and cannot reset actuator
internal state); opaque `SaveState()/RestoreState()` blobs (more general, but
nothing today needs mid-episode restore; the commit snapshot is the cheap
special case); rebuilding the scene (impossible — `CommitScene` freezes it).

### D5: `Robot.reset()` delegates; Python-side snapshot removed

`Robot` keeps name→id maps, batch target/torque helpers and numpy joint views.
`reset()` becomes a call to `app.reset_to_initial()`; `snapshot()` /
`has_snapshot` are removed (BREAKING, pre-1.0 package). The commit-time
snapshot in C++ is strictly earlier and more deterministic than a
user-taken snapshot.

### D6: `ExternalForce`/`ExternalTorque` keep dedicated write-only staging

There is no readback for these fields, so there is nothing to merge with; zero
is a neutral value for untouched slots and written values persist in the
staging across steps (composing multiple writes correctly). `RunActuators`
continues to mark the torque field dirty every step.

## Risks / Trade-offs

- **[Zero-copy view content mutates on write]** — `get_body_states()` arrays
  change in place when `set_body_value` is called, without a `step_id` bump.
  Document: the view shows "current known state"; `step_id` counts steps only.
- **[Single-threaded write assumption]** — writes must happen between `Step`
  calls (already a documented contract of `SetBodyValue`); the shared buffer
  would need locking otherwise. State it on `ResetToInitial` too.
- **[Non-coherent host memory on some devices]** — today's reads assume
  coherent memory; D3 adds defensive Flush/Invalidate so the design is correct
  either way at negligible cost.
- **[Whole-app reset scope]** — a consumer wanting to reset one robot of many
  gets a coarser tool than before (Python could reset a subset). Accepted:
  one robot per app is the supported shape; scoped restore is a compatible
  future addition.
- **[BREAKING Python helper surface]** — `Robot.snapshot()`/`has_snapshot`
  removal; pre-1.0 and covered by the spec delta and test updates.
