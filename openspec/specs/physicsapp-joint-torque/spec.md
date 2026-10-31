# physicsapp-joint-torque

## Purpose

Give `PhysicsApp` a manual joint-torque channel so external control loops (RL policies in particular) can drive joints with direct torques instead of position targets, without owning the actuator machinery.

## Requirements

### Requirement: SetJointTorque applies equal-and-opposite joint torque
`PhysicsApp` SHALL provide `SetJointTorque(JointId joint, float torque)`,
legal only in the Drive phase (after `CommitScene`). The torque SHALL be
applied about the joint's hinge axis as equal-and-opposite world-space torques
(`+torque * axis_world` on the child body, `-torque * axis_world` on the parent
body) through the bodies' external-torque write path, with the axis resolved
internally (the caller never handles coordinate frames). The write SHALL be
caller-managed: the torques persist and keep applying every subsequent step
until the caller overwrites them (including with zero), mirroring
`SetBodyValue(ExternalForce)` lifetime semantics.

#### Scenario: Torque accelerates the joint
- **WHEN** `SetJointTorque(joint, tau)` is called on a free hinge joint and the
  simulation is stepped
- **THEN** the joint accelerates about its axis in the direction a positive
  `tau` implies, and the two bodies receive opposite torques

#### Scenario: Torque persists until cleared
- **WHEN** a torque is set once and the simulation is stepped several times
  without further calls
- **THEN** the torque keeps applying on each step until the caller sets it to
  zero or another value

#### Scenario: Drive-phase only
- **WHEN** `SetJointTorque` is called before `CommitScene`
- **THEN** a `std::logic_error` is thrown

#### Scenario: Invalid joint handle
- **WHEN** `SetJointTorque` is called with a `JointId` that does not exist
- **THEN** a `std::out_of_range` is thrown

### Requirement: Actuators own the torque channel of their joint
`SetJointTorque` SHALL throw `std::logic_error` when the joint has a registered
actuator. Manual torque control uses joints without actuators; actuated joints
keep their per-step actuator torque ownership unchanged.

#### Scenario: Torque on an actuated joint throws
- **WHEN** `SetJointTorque` is called on a joint that has a registered actuator
- **THEN** a `std::logic_error` is thrown and no torque is applied

#### Scenario: Actuator torque path unchanged
- **WHEN** a joint has a registered actuator and the simulation is stepped
- **THEN** the actuator's computed torque is applied exactly as before, with no
  manual-torque interference

### Requirement: Joint torque interacts with body external-torque writes
`SetJointTorque` and `SetBodyValue(ExternalTorque)` SHALL share the same
persistent per-body external-torque field: the most recent write to a body's
field wins until overwritten. An episode reset that clears torques SHALL zero
both channels.

#### Scenario: Later write wins
- **WHEN** `SetJointTorque(joint, tau)` is followed by
  `SetBodyValue(child, ExternalTorque, zero)` before the next step
- **THEN** the child body's external torque is zero for the next step while the
  parent body keeps its component of the joint torque
