## 1. C++ actuator interface

- [x] 1.1 Drop `const` from `Actuator::ComputeTorque` and add `virtual void Actuator::Reset()` with a no-op base implementation (Doxygen updated); verify `test/app/physics/physics_app_actuator_unit_test` builds against the non-const signature and its control-law assertions still pass.
- [x] 1.2 Add readable/writable accessors for every constructor parameter of `PdActuator` (`kp`, `kd`) and `DcMotorActuator` (`kp`, `kd`, `stall_torque`, `no_load_speed`, `cont_torque`, `gear_ratio`); extend `physics_app_actuator_unit_test` to assert read-back and that a written parameter changes the next computed torque (spec scenario "Parameters are readable and tunable").
- [x] 1.3 Add a stateful test actuator (e.g. leaking integrator) to `physics_app_actuator_unit_test` and assert `Reset()` restores freshly-constructed behavior (spec scenario "Reset clears internal control state").

## 2. PhysicsApp registration API

- [x] 2.1 Change `AddActuator` to `std::shared_ptr<Actuator>` and `Impl::m_actuators` to `std::vector<std::shared_ptr<Actuator>>`; keep null/duplicate/`std::out_of_range`/`std::invalid_argument` error behavior; update the three C++ actuator tests to `std::make_shared` and verify `ctest --preset msvc-debug` passes them.
- [x] 2.2 Add `GetActuator(JointId) const` (empty pointer when the joint has none) and `RemoveActuator(JointId)`; removal zeroes both endpoint bodies' `ExternalTorque` and throws `std::logic_error` on a joint without an actuator; extend `physics_app_joint_torque_test` to assert the removed joint's stale torque stops applying and `SetJointTorque` on it is legal again (spec scenarios "Registered instance is retrievable", "Removal clears the torque channel", "Removal hands the channel back to manual control").
- [x] 2.3 Lift the Building-phase restriction: `AddActuator`/`GetActuator`/`RemoveActuator` are legal after `CommitScene` and an added actuator drives from the next `Step`; add a C++ test that registers, tunes, and removes an actuator in the Drive phase (spec scenario "Registration and removal are legal in either phase").

## 3. Python bindings

- [x] 3.1 Delete the `PythonActuator` adapter and its `m_synced_target`; bind `add_actuator` over `std::shared_ptr<Actuator>` and add `get_actuator`/`remove_actuator` (empty pointer surfaces as `None`); verify in Python that `app.get_actuator(joint) is act` and that a registered Python instance survives the caller dropping its reference (spec scenario "Registered actuator is the caller's object").
- [x] 3.2 Wrap `PyActuator`'s virtual overrides in `nb::gil_scoped_acquire` and document the rule (every future `Actuator` virtual must acquire the GIL in its trampoline override) in `binding_support.h`/`physics_app_bindings.cpp`; verify the existing GIL test still passes with a Python actuator driving a joint while `step` releases the GIL.
- [x] 3.3 Bind the parameter accessors as `def_prop_rw` properties on `PdActuator`/`DcMotorActuator`; verify from Python that changing `kp` on a registered actuator changes the motion on the next steps (spec scenario "Live parameter tuning").

## 4. Python tests, examples, docs

- [x] 4.1 Extend `app/python/tests/test_actuator.py` to cover: identity, live tuning, removal handing the joint back to `set_joint_torques`, a Python subclass registered without any wrapper, and add/remove after `commit_scene`; verify the suite passes against the dev build tree.
- [x] 4.2 Update `app/python/tests/test_joint_torque.py`, `app/python/examples/`, and `app/python/README.md` for the new registration API (`shared_ptr` semantics, `get_actuator`/`remove_actuator`, parameter properties); verify every documented command still runs.

## 5. Integration verification

- [x] 5.1 Run `clang-format` on all touched C++ files and verify a clean `cmake --build --preset msvc-debug` plus `ctest --preset msvc-debug` (all physics-app tests pass).
- [x] 5.2 Verify `cmake --build --preset msvc-release` succeeds and the full `app/python/tests` pytest suite passes in both dev-build mode and installed-wheel mode (wheel rebuilt via `python -m build`).
