## MODIFIED Requirements

### Requirement: Actuators own the torque channel of their joint
`SetJointTorque` SHALL throw `std::logic_error` when the joint has a registered
actuator. Manual torque control uses joints without actuators; actuated joints
keep their per-step actuator torque ownership unchanged. Detaching the actuator
(`RemoveActuator`) SHALL return the channel to manual control: the last applied
actuator torque is cleared, after which `SetJointTorque` on that joint is legal
again and its writes follow the same caller-managed persistence as before.

#### Scenario: Torque on an actuated joint throws
- **WHEN** `SetJointTorque` is called on a joint that has a registered actuator
- **THEN** a `std::logic_error` is thrown and no torque is applied

#### Scenario: Actuator torque path unchanged
- **WHEN** a joint has a registered actuator and the simulation is stepped
- **THEN** the actuator's computed torque is applied exactly as before, with no
  manual-torque interference

#### Scenario: Removal hands the channel back to manual control
- **WHEN** an actuator is removed from a joint and `SetJointTorque` is called on
  that joint before the next step
- **THEN** no `std::logic_error` is raised, the stale actuator torque no longer
  applies, and the manually set torque is applied instead
