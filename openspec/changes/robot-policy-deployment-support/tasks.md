# Tasks: Robot Policy Deployment Support

## 1. Revert Ad-Hoc Worktree Changes

- [x] 1.1 Revert the ad-hoc physics-knob plumbing: remove `physics_time_step`/`physics_substeps`/`physics_iterations` from `engine/Core/Functional/OptionHandler.h`, `engine/Framework/MainClass.{h,cpp}` state members and the ternary merge in `LoadProject`, restoring those files to their pre-change behavior (verify `git diff` on those files no longer shows physics fields)
- [x] 1.2 Delete the cross-step torque bookkeeping in `app/physics/PhysicsApp.cpp` (`actuated_slots`, `ReleaseActuatorSlots`, `had_torque` logic and the `RemoveActuator` slot-release call) but keep the accumulate fix in `RunActuators` as the starting point for task 3 (verify the file compiles after task 3's rewrite; at this step verify no references to the deleted symbols remain)

## 2. Engine Seams

- [x] 2.1 Add the optional `const XpbdConfig*` parameter to `MainClass::LoadProject` (default nullptr = engine defaults) with a comment marking it a temporary seam until project-level config exists; verify `test/engine/windowed/project_loading_test` and `physics_registration_test` still pass
- [x] 2.2 Replace the absolute `det > 1e-12` invertibility tests in `engine/Framework/Bridge/Internal/ComInertiaComputer.hpp` with one shared gate helper: double-precision `|det| > ε_machine · ‖I‖_F³` plus a positive-definiteness check, throwing `std::invalid_argument` on an invalid manual tensor; verify the tiny-inertia regression (`app/python/tests/test_inertia.py`) passes and a negative-definite manual inertia raises
- [x] 2.3 Implement per-step consumption of external force/torque in the solver: clear both buffers after the substep loop in `XpbdGpuSolver::GPUStep` (never inside the substep loop), leave them untouched when the step does not integrate, and aggregate the clearing in `PhysicsSystem::GPUStep` for multi-solver scenes; mirror in `DummySolver`. Verify a force applied over a 2+-substep step contributes full `F·dt` and is gone on the next step (scenario in `physics-solver-interface` delta)

## 3. Round-Based Write Channel (PhysicsApp)

- [x] 3.1 Convert the force/torque staging buffers to the same readback-capable type as the state channels (`ReadbackFromDevice | CopyFrom`) and extend the post-step readback copies and invalidations to all six fields; verify `GetBodyState` values after a step reflect the integrated result and a force write does not resurrect across rounds
- [x] 3.2 Rewrite `RunActuators` to accumulate actuator torques per body slot within the round (sum across actuators on shared bodies, zero cross-step state) and simplify `RemoveActuator` to just detach the actuator; verify the chained-joint regression scenario passes (both joints reach targets)
- [x] 3.3 Rename `SetJointTorque` to `AddJointTorque` with `+=` semantics on the round's torque slots (axis resolved at call time as before), keep the actuated-joint guard and phase/handle errors; verify accumulate-twice doubles the torque and the guard still throws
- [x] 3.4 Make `ResetToInitial` discard uncommitted round writes in addition to restoring the snapshot; verify pending writes before a reset do not appear in the next step

## 4. Physics Timing Knobs

- [x] 4.1 Add `physics_time_step`/`physics_substeps`/`physics_iterations` (0 = engine default) to `CreateInfo`, merge them into an `XpbdConfig` in `PhysicsApp::Create`, and pass it through the new `LoadProject` parameter; verify `CreateInfo(physics_time_step=0.005)` steps 5 ms per `Step` and zero knobs reproduce engine defaults
- [x] 4.2 Bind the three fields in `app/python/value_types.cpp` `CreateInfo` and verify the Python scenario `CreateInfo(physics_time_step=0.005)` runs at 200 Hz physics

## 5. Python Surface and Tests

- [x] 5.1 Rename the binding to `add_joint_torque`, update `Robot.set_joint_torques` to accumulate semantics (no `set_joint_torque` alias kept — clean break per proposal), and update `app/python/README.md` API notes (round semantics, re-apply-per-step pattern, physics rate knobs)
- [x] 5.2 Rewrite `app/python/tests/test_joint_torque.py` and the C++ mirror `test/app/physics/physics_app_joint_torque_test.cpp` to round semantics (re-apply per step, one-round force effect, call-order resolution); verify both pass
- [x] 5.3 Rewrite the `test_robot.py` disturbance episode to re-apply force per round and verify the reset scenarios still pass
- [x] 5.4 Keep the chained-joint regression (`test_actuator.py::test_chained_joints_accumulate_their_torque_on_a_shared_body`) and the tiny-inertia regression (`test_inertia.py`) green; add a chained-manual-torque scenario (two `AddJointTorque` calls sharing a body sum correctly); verify the full pytest suite passes
- [x] 5.5 Run the full C++ ctest suite for `test/app/physics` and `test/engine/headless` and verify no regressions vs. the pre-change baseline

## 6. Verification

- [x] 6.1 Run `openspec validate --strict --change "robot-policy-deployment-support"` and verify it passes; run the full python test suite and the physics C++ tests and verify all green
