# physicsapp-episode-reset

## Purpose

Gives `PhysicsApp` an episode-level reset back to the state recorded when the
scene was committed, so RL loops and repeated trials can restart without
replaying per-body writes from outside.

## Requirements

### Requirement: Commit-time state snapshot

`PhysicsApp` SHALL record a snapshot at `CommitScene`, after physics buffers are
seeded, containing every body's pose and velocity and every registered
actuator's target angle. The snapshot SHALL be retained for the app's lifetime
and SHALL NOT require any caller-side recording call.

#### Scenario: Snapshot is taken at commit

- **WHEN** a scene is committed and the simulation runs for several steps
- **THEN** the recorded snapshot still holds the state as of `CommitScene`, not
  the later state

#### Scenario: No caller-side snapshot call

- **WHEN** a consumer builds a scene, commits and later calls reset
- **THEN** reset works without any prior recording call by the consumer

### Requirement: ResetToInitial restores the commit-time state

`PhysicsApp` SHALL provide `void ResetToInitial()` callable after `CommitScene`
(Drive phase). It SHALL restore every body's position and rotation to the
snapshot and write the snapshot velocities back as integration seeds (the next
step derives velocity from the pose change, as elsewhere in the write API). It
SHALL zero `ExternalForce` and `ExternalTorque` for every body, restore every
actuator's target angle to its commit-time value, and reset actuator internal
state through `Actuator::Reset()`.

Restored state SHALL be visible to state reads immediately, and SHALL take
effect in the physics buffers on the next `Step`. Reset SHALL NOT advance the
simulation or render.

Calling `ResetToInitial` before `CommitScene` SHALL throw `std::logic_error`.

#### Scenario: Reset returns bodies to the commit pose

- **WHEN** bodies are disturbed by an episode of simulation and `ResetToInitial` is called
- **THEN** `GetBodyState` reports the commit-time positions and rotations
  immediately, without an intervening step

#### Scenario: Reset clears external forces and torques

- **WHEN** external force or torque was set on any body and `ResetToInitial` is called
- **THEN** subsequent steps apply no residual force or torque from before the reset

#### Scenario: Reset restores actuator targets and internal state

- **WHEN** an actuator's target was changed during the episode and it holds
  internal state (e.g. a filter or integrator), and `ResetToInitial` is called
- **THEN** the target is back at its commit-time value and the actuator's
  internal state is cleared

#### Scenario: Reset before commit rejected

- **WHEN** `ResetToInitial` is called before `CommitScene`
- **THEN** `std::logic_error` is thrown

#### Scenario: Reset does not step the simulation

- **WHEN** `ResetToInitial` is called and no `Step` follows
- **THEN** physics has not advanced and no frame was rendered
