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

### Requirement: Writes take effect for one round

Every `SetBodyValue` write SHALL take effect for one round — the interval until the next `Step` consumes it. A step's upload carries the round's complete per-field picture (written slots plus the current baseline for every other slot), the solve applies it, and afterwards the written values are gone.

State fields (`Position`, `Rotation`, `LinearVelocity`, `AngularVelocity`) carry their effect forward through the simulated state itself: a position set once starts the body from that pose and integration evolves it naturally from there, and the post-step readback replaces the staging baseline so stale state writes never resurface.

`ExternalForce` and `ExternalTorque` SHALL be per-step quantities: the step's integration applies them and the solver clears them afterwards (after the last substep of the step). A force or torque set once therefore acts for exactly one step, contributing an impulse of `value * dt` over that step regardless of the substep count. Keeping a force acting SHALL require re-applying it every round.

Within one round, writes SHALL resolve by call order per slot: a later `SetBodyValue` assignment overwrites whatever landed on that slot earlier in the round (including accumulated joint-torque contributions), and a value from an earlier round SHALL NOT resurface in a later one.

A `Step` that does not integrate (paused / simulation disabled) SHALL NOT consume the round's `ExternalForce`/`ExternalTorque`: those values stay pending and apply on the next step that actually integrates.

#### Scenario: Force applies for exactly one step

- **WHEN** `SetBodyValue(id, ExternalForce, F)` is called and `Step` runs twice
  with no further writes
- **THEN** the force acts on the first step's integration only (impulse
  `F * dt`) and the second step applies no residual force

#### Scenario: Sustained force requires re-application

- **WHEN** `SetBodyValue(id, ExternalForce, F)` is called before every `Step` in
  a loop
- **THEN** the force keeps acting on each step of the loop

#### Scenario: No resurrection across rounds

- **WHEN** a force is written to one body in one round and a different body's
  force is written in the next round
- **THEN** the first body's force does not reappear in the second round's step

#### Scenario: Position set once does not snap back

- **WHEN** `SetBodyValue(id, Position, P)` is called once and `Step` runs twice
  without further writes
- **THEN** after the first step the body's position starts from P
- **AND** after the second step the body's position has evolved from the
  integrated state (it is not reset to P)

#### Scenario: Later write wins within the round

- **WHEN** `AddJointTorque(joint, tau)` is followed by
  `SetBodyValue(child, ExternalTorque, zero)` before the next step
- **THEN** the child body's external torque is zero for that step while the
  parent body keeps its component of the joint torque

#### Scenario: Pending force survives pause

- **WHEN** `SetBodyValue(id, ExternalForce, F)` is called while `Step` does not
  integrate (paused), and the simulation later resumes and steps
- **THEN** the force applies on the first step that integrates and is consumed
  there

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
