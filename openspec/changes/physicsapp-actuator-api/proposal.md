## Why

`PhysicsApp::AddActuator` takes exclusive `std::unique_ptr` ownership of an actuator, only during the Building phase, and the caller never sees the object again (the only control channel is `SetTargetAngle(JointId, x)`). This blocks the next round of work: several C++ actuator types are planned and each needs live parameter tuning from Python, but `PdActuator`/`DcMotorActuator` expose no parameter accessors and the registered instance is unreachable. It also blocks Python-defined actuators from taking the same path as C++ ones — nanobind cannot relinquish ownership of a Python-owned instance into `std::unique_ptr`, so the binding layer currently hides every Python actuator behind a wrapper adapter with shadow target state. Finally, `ComputeTorque` is `const`, so stateful control laws (friction, backlash, filters, integrators) cannot be expressed at all.

## What Changes

- **BREAKING**: `AddActuator(JointId, std::shared_ptr<Actuator>)` replaces `AddActuator(JointId, std::unique_ptr<Actuator>)`; the app shares ownership with the caller and registration becomes legal in both phases (Building and Drive).
- New `std::shared_ptr<Actuator> GetActuator(JointId) const` returns the registered instance (same object identity in Python), and `void RemoveActuator(JointId)` detaches it and clears the joint's external-torque channel.
- **BREAKING**: `Actuator::ComputeTorque` drops `const` so control laws may keep internal state; a new `virtual void Reset()` clears that state (episode resets call it).
- `PdActuator` / `DcMotorActuator` gain readable, writable parameters (`kp`, `kd`, `stall_torque`, `no_load_speed`, `cont_torque`, `gear_ratio`).
- Python surface: `add_actuator` accepts any `Actuator` — a C++ type or a Python subclass — with no wrapper; `get_actuator` / `remove_actuator` are added; actuator parameters are settable properties for live tuning. The binding-layer `PythonActuator` adapter and its mirrored-target state are deleted; the Python subclass trampoline acquires the GIL around the callback itself so `step` keeps releasing it.
- Out of scope: time-step plumbing. `ComputeTorque` stays time-step free and types needing `dt` keep receiving it as a constructor parameter (a future change exposes the physics configuration).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `physicsapp-joint-actuators`: actuator registration (shared ownership, both phases, retrieval and removal), the `Actuator` base-class contract (`const` removal, `Reset()`, parameter accessors).
- `physicsapp-joint-torque`: the "actuator owns the torque channel" rule is extended to actuator removal (the channel is cleared so manual torque control can take over).
- `physicsapp-python-bindings`: the actuator surface (`add_actuator`/`get_actuator`/`remove_actuator`, parameter properties, shared identity between Python and the app) and where the GIL is acquired for subclass callbacks.

## Impact

- C++: `app/physics/Actuator.{h,cpp}` (base contract, accessors, `Reset()`), `app/physics/PhysicsApp.{h,cpp}` (`AddActuator`/`GetActuator`/`RemoveActuator`, phase rules); call sites in `test/app/physics/` (`physics_app_actuator_unit_test`, `physics_app_actuator_stand_test`, `physics_app_joint_torque_test`).
- Bindings: `app/python/physics_app_bindings.cpp` (adapter removal, `shared_ptr` registration, parameter properties, trampoline GIL), `app/python/value_types.cpp` unchanged.
- Python: `app/python/tests/` (actuator identity, live tuning, removal), `app/python/examples/`, `app/python/README.md`.
- No engine/solver changes, no asset or data-format changes.
