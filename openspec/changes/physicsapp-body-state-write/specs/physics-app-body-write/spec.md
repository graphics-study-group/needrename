## ADDED Requirements

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

## MODIFIED Requirements

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

## REMOVED Requirements

### Requirement: Writes are uploaded before the physics step
**Reason**: Its "Values are invisible before the step" scenario codified write
invisibility, which this change reverses: writes are observable in state reads
immediately, and the whole-field upload must not clobber slots the caller never
touched. The upload contract is restated, without that scenario, under "Writes
are uploaded per dirty field before the solve".
**Migration**: Follow the added requirement — same upload timing and per-dirty-
field batching; the behavioral difference is that reads observe writes
immediately and unwritten slots keep their simulated state.
