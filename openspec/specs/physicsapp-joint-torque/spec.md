# physicsapp-joint-torque

## Purpose

Give `PhysicsApp` a manual joint-torque channel so external control loops (RL policies in particular) can drive joints with direct torques instead of position targets, without owning the actuator machinery.

## Requirements

### Requirement: AddJointTorque accumulates equal-and-opposite joint torque

`PhysicsApp` SHALL provide `AddJointTorque(JointId joint, float torque)`, legal
only in the Drive phase (after `CommitScene`). Each call SHALL accumulate
(`+=`) equal-and-opposite world-space torques about the joint's hinge axis
(`+torque * axis_world` on the child body, `-torque * axis_world` on the parent
body) into the current round's external-torque values, with the axis resolved
internally (the caller never handles coordinate frames).

Accumulation SHALL make serial chains compose: a body that is the child of one
joint and the parent of another receives the sum of both joints'
equal-and-opposite contributions within the round.

The accumulated torques take effect on the next `Step` and are consumed with
it, matching the one-round write lifetime of `SetBodyValue`: keeping a joint
torque acting across steps SHALL require re-applying it every round. The calls
are not idempotent: two calls with the same value in one round produce twice
the torque.

#### Scenario: Torque accelerates the joint

- **WHEN** `AddJointTorque(joint, tau)` is called on a free hinge joint and the
  simulation is stepped
- **THEN** the joint accelerates about its axis in the direction a positive
  `tau` implies, and the two bodies receive opposite torques

#### Scenario: Torque lasts exactly one round

- **WHEN** `AddJointTorque(joint, tau)` is called once and the simulation is
  stepped several times without further calls
- **THEN** the torque applies on the first step only

#### Scenario: Accumulation is additive within a round

- **WHEN** `AddJointTorque(joint, tau)` is called twice with the same value
  before the next step
- **THEN** the step applies a torque of `2 * tau` to the joint

#### Scenario: Chained joints both contribute to the shared body

- **WHEN** two hinge joints share a middle body and
  `AddJointTorque` is called on both before the next step
- **THEN** the middle body receives the sum of both joints' contributions
  (the later call does not erase the earlier one)

#### Scenario: Drive-phase only

- **WHEN** `AddJointTorque` is called before `CommitScene`
- **THEN** a `std::logic_error` is thrown

#### Scenario: Invalid joint handle

- **WHEN** `AddJointTorque` is called with a `JointId` that does not exist
- **THEN** a `std::out_of_range` is thrown

### Requirement: Actuators own the torque channel of their joint

`AddJointTorque` SHALL throw `std::logic_error` when the joint has a registered
actuator. Manual torque control uses joints without actuators; actuated joints
keep their per-step actuator torque ownership unchanged. Detaching the actuator
(`RemoveActuator`) SHALL return the channel to manual control: no stale actuator
torque remains to be cleared (actuator torques are per-round like every other
write), after which `AddJointTorque` on that joint is legal again.

#### Scenario: Torque on an actuated joint throws
- **WHEN** `AddJointTorque` is called on a joint that has a registered actuator
- **THEN** a `std::logic_error` is thrown and no torque is applied

#### Scenario: Actuator torque path unchanged
- **WHEN** a joint has a registered actuator and the simulation is stepped
- **THEN** the actuator's computed torque is applied exactly as before, with no
  manual-torque interference

#### Scenario: Removal hands the channel back to manual control
- **WHEN** an actuator is removed from a joint and `AddJointTorque` is called on
  that joint before the next step
- **THEN** no `std::logic_error` is raised, and the manually added torque is
  applied on the next step

### Requirement: Joint torque interacts with body external-torque writes

`AddJointTorque` and `SetBodyValue(ExternalTorque)` SHALL share the same
per-round external-torque values and resolve by call order per slot: an
accumulate call adds to whatever is currently in the round's slot values, an
assignment overwrites the slot, and the last operation on a slot before the
next `Step` determines what that step applies. The caller SHALL be responsible
for write order. An episode reset SHALL discard uncommitted round writes from
both channels.

#### Scenario: Later write wins
- **WHEN** `AddJointTorque(joint, tau)` is followed by
  `SetBodyValue(child, ExternalTorque, zero)` before the next step
- **THEN** the child body's external torque is zero for the next step while the
  parent body keeps its component of the joint torque

#### Scenario: Earlier assignment is not re-applied later in the round
- **WHEN** `SetBodyValue(child, ExternalTorque, X)` is followed by
  `AddJointTorque(joint, tau)` on a joint touching that body before the next
  step
- **THEN** the step applies `X + tau * axis_world` to the child body (the
  accumulate adds to the assignment in call order)
