# physics-app-body-write

## Purpose

Drive-phase per-body state write API on `PhysicsApp`: override rigid-body position, rotation, linear/angular velocity and external force/torque before each `Step`, with upload to the GPU physics buffers recorded ahead of the solver dispatch.

## Requirements

### Requirement: Drive-phase per-body write API

`PhysicsApp` SHALL expose `SetBodyValue(BodyId id, BodyField field, glm::vec4 value)` callable after `CommitScene` (Drive phase).

`BodyField` SHALL contain exactly the following six enumerators: `Position`, `Rotation`, `LinearVelocity`, `AngularVelocity`, `ExternalForce`, `ExternalTorque`. No other body properties SHALL be writable through this API.

Calling `SetBodyValue` before `CommitScene` SHALL throw `std::logic_error`. Calling it with a `BodyId` that was not returned by an Add/LoadUrdf method SHALL throw `std::out_of_range`.

#### Scenario: Set throws before commit

- **WHEN** `SetBodyValue` is called on an app that has not called `CommitScene`
- **THEN** `std::logic_error` is thrown

#### Scenario: Set throws for invalid body id

- **WHEN** `SetBodyValue` is called with a `BodyId` never returned by an Add method
- **THEN** `std::out_of_range` is thrown

#### Scenario: Only six fields are writable

- **WHEN** the `BodyField` enum is inspected
- **THEN** it contains exactly `Position`, `Rotation`, `LinearVelocity`, `AngularVelocity`, `ExternalForce`, `ExternalTorque`

### Requirement: Value layout is a single vec4 per field

The `value` argument SHALL be a single `glm::vec4` for every field:

- `Rotation` SHALL store a quaternion as xyzw.
- `Position`, `LinearVelocity`, `AngularVelocity`, `ExternalForce`, `ExternalTorque` SHALL store the 3-component quantity in xyz with w = 0.

All values SHALL be in physics (COM) world space, matching `BodyState` / `BodyStatesView` conventions.

#### Scenario: Rotation stores quaternion xyzw

- **WHEN** `SetBodyValue(id, BodyField::Rotation, value)` is called with `value = (x, y, z, w)` of a quaternion
- **THEN** the body's rotation after the next `Step` corresponds to that quaternion

#### Scenario: Vec3 fields ignore w

- **WHEN** `SetBodyValue(id, BodyField::LinearVelocity, glm::vec4(1, 0, 0, 7))` is called
- **THEN** the body's linear velocity after the next `Step` has x = 1 (w does not affect behavior)

### Requirement: Direct overwrite with caller-managed lifetime

A set value SHALL directly replace the corresponding GPU buffer slot at the next `Step` and SHALL persist until overwritten by another `SetBodyValue` call.

`ExternalForce` and `ExternalTorque` SHALL NOT be cleared by the solver: a set force/torque keeps being applied by every subsequent step's integration until the caller sets it back to zero. Position/rotation/velocity values SHALL evolve naturally under integration after they are applied (no re-application of stale values on later steps).

Multiple calls for the same field within one step interval SHALL resolve to the last written value.

#### Scenario: Force persists until caller zeroes it

- **WHEN** a body is in free fall, `SetBodyValue(id, ExternalForce, F)` is called with an upward force, and `Step` runs
- **THEN** the body's downward acceleration is reduced on that step
- **AND** the same upward force keeps acting on every subsequent `Step` until `SetBodyValue(id, ExternalForce, zero)` is called

#### Scenario: No solver-side force clearing

- **WHEN** a force is set once and `Step` runs many times without further writes
- **THEN** the force buffer value survives every step (the body keeps being pushed)

#### Scenario: Position set once does not snap back

- **WHEN** `SetBodyValue(id, Position, P)` is called once and `Step` runs twice without further writes
- **THEN** after the first step the body's position starts from P
- **AND** after the second step the body's position has evolved from the integrated state (it is not reset to P)

### Requirement: Writes are uploaded per dirty field before the solve

`Step` SHALL record upload copies from the app's host write buffers into the
physics GPU buffers BEFORE recording `GPUStep`, so the set values are visible to
the solver within the same step.

The upload SHALL cover exactly the fields dirtied by `SetBodyValue` since the
previous `Step` (whole-field upload per dirty field). Fields that were not set
SHALL NOT be re-uploaded. Dirty tracking SHALL be per field: any number of slot
writes to one field within one step interval SHALL produce exactly one upload of
that field on the next step.

For every slot the caller has not written since the last `Step`, the upload
SHALL leave that body's simulated state unchanged: writing one body's
`Position`, `Rotation`, `LinearVelocity` or `AngularVelocity` SHALL NOT move,
rotate or re-velocity any other body.

Host writes SHALL NOT require per-call synchronization with the device; pending
writes are made device-visible once per step and per dirty field.

#### Scenario: Set position takes effect on the next step

- **WHEN** a static body's position is set to P and `Step` is called once
- **THEN** `GetBodyState(id)` after that step reflects a state integrated from P

#### Scenario: Unset fields are not re-uploaded

- **WHEN** a body's velocity is set once, `Step` runs, and then `Step` runs again without any `SetBodyValue` calls
- **THEN** the velocity continues evolving from the integrated state instead of being reset to the previously set value

#### Scenario: Writing one body does not move the others

- **WHEN** a scene contains three bodies, `SetBodyValue(b1, Position, P)` is called, and `Step` runs once
- **THEN** `b1` moves to P while `b0` and `b2` continue from their own simulated states (they are not teleported or zeroed)

#### Scenario: Repeated writes to one field cost one upload

- **WHEN** `SetBodyValue` writes many slots of one field between two `Step` calls
- **THEN** the next `Step` uploads that field exactly once and the writes resolve to the last written value per slot

### Requirement: Readback stays consistent with writes

`GetBodyState` / `GetBodyStates` / `GetJointState` SHALL return the app's
current known body state: values written by `SetBodyValue` (or by episode
reset) SHALL be visible to these reads immediately, before any `Step` runs.
After a step, the readback (recorded after the solver dispatch in the same
command buffer) SHALL reflect the integrated result of that step, replacing the
pending values.

#### Scenario: Readback reflects integrated write

- **WHEN** `SetBodyValue(id, LinearVelocity, V)` is called on a free body and `Step` runs once
- **THEN** `GetBodyState(id)` returns a position advanced from the body's previous position by the integrated velocity

#### Scenario: Write is visible to state reads immediately

- **WHEN** `SetBodyValue(id, Position, P)` is called and `GetBodyState(id)` is called before the next `Step`
- **THEN** the returned position is P (the write needs no intervening step to become observable)

#### Scenario: Actuator measurement sees the written state

- **WHEN** a body's pose is written between two steps and an actuator is registered on a joint of that body
- **THEN** the actuator's next torque computation measures the written pose, not the pose from before the write
