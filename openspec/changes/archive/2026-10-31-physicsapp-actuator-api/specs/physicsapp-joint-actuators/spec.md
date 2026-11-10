## MODIFIED Requirements

### Requirement: Actuator base class with PD and DCMotor specializations

`PhysicsApp` SHALL expose an `Actuator` base class (AppPhysics namespace) with:
- a virtual `float ComputeTorque(float angle, float angular_velocity)` pure function,
- a non-virtual `SetTargetAngle(float)` / `float GetTargetAngle()` pair storing the target in the base class,
- a virtual `void Reset()` returning the actuator to its freshly constructed state (the base implementation is a no-op),
- a virtual destructor.

`ComputeTorque` SHALL NOT be const: a control law MAY keep internal state across calls (filters, integrators, friction or backlash models SHALL be expressible as `Actuator` subclasses), and `Reset()` SHALL clear that state.

`PdActuator` SHALL compute `τ = kp·wrap(target − angle) + kd·(0 − angular_velocity)` where `wrap` maps the angle error into `[−π, π)`. Its constructor SHALL accept `kp` and `kd` with defaults `kp = 25.0f`, `kd = 0.5f`.

`DcMotorActuator` SHALL compute the same PD core, then clip the torque with the four-quadrant DC-motor torque-speed envelope: torque bound at the current joint velocity `ω` is derived from `stall_torque`, `no_load_speed`, `cont_torque`, `gear_ratio` (joint velocity scaled to the motor side by `gear_ratio`), and the result is bounded by `±cont_torque`. Its constructor SHALL accept all six parameters with defaults `kp = 25.0f`, `kd = 0.5f`, `stall_torque = 33.5f`, `no_load_speed = 21.0f`, `cont_torque = 13.4f`, `gear_ratio = 1.0f`.

`PdActuator` and `DcMotorActuator` SHALL expose every constructor parameter as a readable and writable accessor (`kp` and `kd`; `stall_torque`, `no_load_speed`, `cont_torque`, `gear_ratio` additionally for `DcMotorActuator`). Writing a parameter SHALL affect every torque computed from the next call onwards.

`ComputeTorque` SHALL NOT depend on a time step; future actuator types that need one SHALL take it as a constructor parameter.

#### Scenario: PD torque follows the control law
- **WHEN** a `PdActuator` with `kp = 10`, `kd = 2` has target `1.0` and is fed `angle = 0.8`, `angular_velocity = 0.1`
- **THEN** `ComputeTorque` returns `10·0.2 − 2·0.1 = 1.8`

#### Scenario: Angle error wraps across ±π
- **WHEN** a `PdActuator` has target `3.1` and is fed `angle = −3.1`
- **THEN** the wrapped error is about `−0.04` (not `6.2`), so the torque is small and negative

#### Scenario: DCMotor clips at the envelope
- **WHEN** a `DcMotorActuator` computes a raw PD torque above `cont_torque`
- **THEN** the returned torque is clipped to `cont_torque`
- **AND** when the joint velocity is negative and large, the returned torque is bounded below by `−cont_torque`

#### Scenario: Parameters are readable and tunable
- **WHEN** the `kp` of a `PdActuator` is written through its accessor after construction
- **THEN** the new value is readable from the accessor and the next computed torque uses it

#### Scenario: Reset clears internal control state
- **WHEN** an `Actuator` subclass has accumulated internal control state and `Reset()` is called
- **THEN** subsequent `ComputeTorque` calls behave as if the actuator had just been constructed

## REMOVED Requirements

### Requirement: Actuators are registered per joint in the Building phase

**Reason**: Replaced by shared-ownership registration that is legal in both phases and adds retrieval and removal; the old block's "AddActuator after commit rejected" scenario is the behavior being reversed.

**Migration**: Call `AddActuator(JointId, std::shared_ptr<Actuator>)` (Building or Drive phase) and keep using your own handle to the instance; use `GetActuator` to retrieve it and `RemoveActuator` to detach or replace it. There is no replacement for the Building-phase-only restriction: registration after `CommitScene` is now allowed.

## ADDED Requirements

### Requirement: Actuators are registered per joint in either phase

`PhysicsApp` SHALL provide `void AddActuator(JointId joint, std::shared_ptr<Actuator> actuator)` legal in both the Building phase and the Drive phase. Ownership SHALL be shared: the app keeps the actuator alive while it is registered, the caller may keep and continue to use the same instance, and the registered instance SHALL be the object the caller passed in (no copy, no wrapper).

`PhysicsApp` SHALL provide `std::shared_ptr<Actuator> GetActuator(JointId joint) const`, returning the registered actuator or an empty pointer when the joint has none, and `void RemoveActuator(JointId joint)`, detaching the actuator from the joint. `RemoveActuator` SHALL also clear the joint's external-torque channel: the torque the actuator applied last stops being applied, leaving the joint available for manual torque control.

At most one actuator SHALL exist per joint: calling `AddActuator` on a joint that already has one SHALL throw `std::invalid_argument` (remove it first to replace). Passing a null actuator SHALL throw `std::invalid_argument`. An invalid `JointId` SHALL throw `std::out_of_range` from all three calls. `RemoveActuator` on a joint without an actuator SHALL throw `std::logic_error`.

An actuator added after `CommitScene` SHALL drive its joint from the next `Step`. The actuator is destroyed when the last shared owner releases it; `PhysicsApp` releasing its share (including at app destruction) SHALL NOT destroy an instance the caller still holds.

#### Scenario: Register a DCMotor actuator on a URDF joint
- **WHEN** `AddActuator` is called with a `JointId` from `LoadUrdf` and a `std::make_shared<DcMotorActuator>()`
- **THEN** the joint is driven by the actuator on subsequent `Step` calls and the caller's `shared_ptr` remains valid and usable

#### Scenario: Registered instance is retrievable
- **WHEN** `GetActuator` is called on a joint whose actuator was registered earlier
- **THEN** it returns the same instance that was registered, usable for parameter tuning

#### Scenario: Duplicate actuator rejected
- **WHEN** `AddActuator` is called twice for the same joint
- **THEN** the second call throws `std::invalid_argument`

#### Scenario: Registration and removal are legal in either phase
- **WHEN** `AddActuator` and `RemoveActuator` are called after `CommitScene`
- **THEN** no phase error is raised and the change takes effect on subsequent `Step` calls

#### Scenario: Removal clears the torque channel
- **WHEN** an actuator has been applying torque to a joint and `RemoveActuator` is called
- **THEN** the joint's external torque is cleared (no stale actuator torque keeps applying) and `SetJointTorque` on that joint no longer throws

#### Scenario: Invalid joint handle
- **WHEN** `AddActuator`, `GetActuator`, or `RemoveActuator` is called with a `JointId` that does not exist
- **THEN** a `std::out_of_range` is thrown
