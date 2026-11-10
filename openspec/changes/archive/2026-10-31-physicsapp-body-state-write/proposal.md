# Proposal: PhysicsApp body-state write semantics and episode reset

## Why

The drive-phase body write path has two problems that make episode resets and
dynamic interventions unreliable. First, writes are invisible to every read:
`SetBodyValue` feeds only the upload staging, while `GetBodyState`,
`GetJointState`, actuator measurement and `SetJointTorque` all read the
step-end readback staging, so the first `Step` after a teleport computes
actuator torques from the pre-reset pose and Python observes stale state until
one step has run. Second, the upload is a whole-field copy from a zero-seeded
staging buffer, so writing one body's `Position`/`Rotation`/`LinearVelocity`/
`AngularVelocity` uploads every slot of that field and teleports all bodies the
caller never wrote to the origin (or to stale values) — a latent corruption
masked only by test scenes that write every body. Episode reset currently works
around both by replaying six fields per body through `SetBodyValue` from a
Python-side snapshot; that keeps the bugs alive and cannot express "back to the
start of the episode" as a single app-level operation.

## What Changes

- Merge the per-field readback and write host buffers for the four readback-
  backed fields (`Position`, `Rotation`, `LinearVelocity`, `AngularVelocity`)
  into one shared staging buffer per field, holding "last simulated state +
  pending writes". Writes become immediately visible to state reads and to
  actuator measurement; whole-field upload stops clobbering unwritten slots.
- Keep dirty tracking at field granularity and whole-field upload per dirty
  field per step (no per-slot bookkeeping); move per-call `Flush` to one flush
  per dirty field at step time, plus defensive `Invalidate` before CPU reads.
- Add `PhysicsApp::ResetToInitial()` (Drive phase): restore every body to the
  state recorded at `CommitScene` (pose plus velocity-as-integration-seed),
  zero all external forces/torques, restore actuator targets and reset actuator
  internal state via `Actuator::Reset()`.
- **BREAKING** Python `Robot.reset()` becomes a thin delegate to the app-level
  reset; `Robot.snapshot()` / `Robot.has_snapshot` are removed (the snapshot now
  lives in C++ and is taken at `commit_scene`).
- Tests: a three-body regression proving one body's write does not move the
  others, read-after-write assertions replacing the "invisible before Step"
  assertion, and tightened robot-reset assertions (no compensating `step()`).

## Capabilities

### New Capabilities

- `physicsapp-episode-reset`: commit-time scene state snapshot and app-level
  `ResetToInitial()` restore, including actuator targets and actuator internal
  state.

### Modified Capabilities

- `physics-app-body-write`: writes are visible to state reads immediately;
  whole-field upload SHALL NOT alter slots the caller did not write (shared
  staging invariant); upload batching and flush placement are specified.
- `physicsapp-python-bindings`: robot `reset` restores the state recorded at
  `commit_scene` through the app-level reset; the Python-side snapshot API is
  removed.

## Impact

- `app/physics/PhysicsApp.h` / `PhysicsApp.cpp`: `Impl` staging buffers (merge
  readback/write for four fields), `SetBodyValue`, `BuildPhysicsReadback`/
  `BuildPhysicsWrite`, `RecordBodyStateUpload`, `Step` (flush/invalidate
  placement), new `ResetToInitial()` plus the commit-time snapshot storage.
- `app/python/`: `reset_to_initial` binding; `src/needrename_sim/robot.py`
  reset delegation and snapshot removal.
- Tests: `test/app/physics/physics_app_physics_only_test.cpp` (visibility
  assertion flip, no-clobber regression), a new reset test,
  `app/python/tests/test_robot.py` (tighter assertions, workaround removed).
- Specs: `physics-app-body-write` (modified), `physicsapp-episode-reset` (new),
  `physicsapp-python-bindings` (modified).
