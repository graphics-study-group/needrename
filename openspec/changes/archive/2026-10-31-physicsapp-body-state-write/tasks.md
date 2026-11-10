## 1. Shared host staging (readback + write merge)

- [x] 1.1 Merge the four readback-backed fields (`Position`, `Rotation`,
  `LinearVelocity`, `AngularVelocity`) into one shared host buffer per field
  (buffer usage gains `CopyFrom`; `SetBodyValue`, `GetBodyState(s)`,
  `GetJointState`, `RunActuators`, `RecordBodyStateUpload` and the step-end
  readback all use it); keep dedicated write-only staging for
  `ExternalForce`/`ExternalTorque`. Verify: `cmake --build --preset msvc-debug`
  succeeds and `ctest --preset msvc-debug` passes unchanged.
- [x] 1.2 Remove the per-call `Flush` from `SetBodyValue`; flush each dirty
  field's range once in `Step` before recording uploads and `Invalidate` the
  shared buffers after the step-end readback before CPU reads. Verify:
  `ctest --preset msvc-debug` passes (write tests included).

## 2. Write semantics tests

- [x] 2.1 Replace the "Set position not visible before Step" assertion in
  `physics_app_physics_only_test.cpp` with read-after-write assertions
  (`GetBodyState`/`GetJointState` reflect the write before any step; actuator
  measurement uses the written pose). Verify: the test passes with the new
  expectations.
- [x] 2.2 Add a three-body regression: after stepping, write one body's
  `Position`, step once, and assert the other two bodies continue from their
  own simulated states (no teleport/zeroing). Verify: the new checks fail on
  the pre-change code and pass after.

## 3. Episode reset

- [x] 3.1 Record the commit-time snapshot in `CommitScene` after the seed copy:
  every slot's pose/velocity plus every actuator's target angle. Verify: a
  test asserts the snapshot is unchanged by later steps (reset returns the
  commit-time state, not a mid-episode one).
- [x] 3.2 Implement `PhysicsApp::ResetToInitial()` (Drive phase; restore pose
  and velocity-as-seed for all bodies, zero `ExternalForce`/`ExternalTorque`,
  restore actuator targets, call `Actuator::Reset()` on every actuator;
  visible to reads immediately; `std::logic_error` before `CommitScene`) and
  cover it with a new `physics_app_reset_test`: disturb an episode, reset,
  assert commit-time poses read back immediately, forces are cleared, targets
  restored, stateful actuator state cleared, and reset performs no step.
  Verify: `ctest --preset msvc-debug` passes including the new test.

## 4. Python layer

- [x] 4.1 Bind `reset_to_initial` on `PhysicsApp`; make `Robot.reset()` delegate
  to it and remove `Robot.snapshot()`/`Robot.has_snapshot` (BREAKING), keeping
  name lookup and batch helpers. Verify: `app/python/tests/test_robot.py`
  passes in dev-build mode.
- [x] 4.2 Tighten `test_robot.py`: drop the compensating `step()` after reset,
  assert commit-time state is readable immediately, and tighten the residual
  angle bound (no post-reset one-step torque spike). Verify: pytest passes.

## 5. Docs and validation

- [x] 5.1 Update `PhysicsApp.h` documentation (`SetBodyValue` read-after-write
  semantics, whole-field upload contract, `ResetToInitial`, single-threaded
  write assumption) and `robot.py` docstrings. Verify: docs describe the new
  behavior and no stale wording remains (grep for "not reflected"/"snapshot").
- [x] 5.2 Run clang-format on touched files and the full suites:
  `ctest --preset msvc-debug`, `pytest app/python/tests` (dev-build mode), a
  rebuilt wheel exercised in `build/clean-venv`, and `openspec validate` on the
  change. Verify: all green and validation passes.
