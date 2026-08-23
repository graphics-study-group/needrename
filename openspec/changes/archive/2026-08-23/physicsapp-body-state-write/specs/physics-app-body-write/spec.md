# physics-app-body-write

## Purpose

Drive-phase per-body state write API on `PhysicsApp`: override rigid-body position, rotation, linear/angular velocity and external force/torque before each `Step`, with upload to the GPU physics buffers recorded ahead of the solver dispatch.

## ADDED Requirements

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

### Requirement: Writes are uploaded before the physics step

`Step` SHALL record upload copies from the app's write staging into the physics GPU buffers BEFORE recording `GPUStep`, so the set values are visible to the solver within the same step.

The upload SHALL cover exactly the fields dirtied by `SetBodyValue` since the previous `Step` (whole-field upload per dirty field). Fields that were not set SHALL NOT be re-uploaded.

#### Scenario: Set position takes effect on the next step

- **WHEN** a static body's position is set to P and `Step` is called once
- **THEN** `GetBodyState(id)` after that step reflects a state integrated from P

#### Scenario: Unset fields are not re-uploaded

- **WHEN** a body's velocity is set once, `Step` runs, and then `Step` runs again without any `SetBodyValue` calls
- **THEN** the velocity continues evolving from the integrated state instead of being reset to the previously set value

#### Scenario: Values are invisible before the step

- **WHEN** `SetBodyValue(id, Position, P)` is called and `GetBodyState(id)` is called before the next `Step`
- **THEN** the returned state is the pre-set state (writes apply only at the next `Step`)

### Requirement: Readback stays consistent with writes

`GetBodyState` / `GetBodyStates` SHALL keep returning the post-step GPU state (readback copy recorded after the solver dispatch in the same command buffer). After a step that applied a write, the readback SHALL reflect the integrated result of that write.

#### Scenario: Readback reflects integrated write

- **WHEN** `SetBodyValue(id, LinearVelocity, V)` is called on a free body and `Step` runs once
- **THEN** `GetBodyState(id)` returns a position advanced from the body's previous position by the integrated velocity
