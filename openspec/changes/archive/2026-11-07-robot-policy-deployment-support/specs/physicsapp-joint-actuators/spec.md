# physicsapp-joint-actuators Delta

## MODIFIED Requirements

### Requirement: Actuators are registered per joint in either phase

`PhysicsApp` SHALL provide `void AddActuator(JointId joint, std::shared_ptr<Actuator> actuator)` legal in both the Building phase and the Drive phase. Ownership SHALL be shared: the app keeps the actuator alive while it is registered, the caller may keep and continue to use the same instance, and the registered instance SHALL be the object the caller passed in (no copy, no wrapper).

`PhysicsApp` SHALL provide `std::shared_ptr<Actuator> GetActuator(JointId joint) const`, returning the registered actuator or an empty pointer when the joint has none, and `void RemoveActuator(JointId joint)`, detaching the actuator from the joint. Removal SHALL NOT need to clear any torque state: actuator contributions live only in the round that produced them (see the per-step write lifetime), so a detached actuator stops influencing the joint on the next step and the joint is immediately available for `AddJointTorque`.

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
- **THEN** no actuator torque keeps applying on later steps (its contribution was per-round) and `AddJointTorque` on that joint no longer throws

#### Scenario: Invalid joint handle
- **WHEN** `AddActuator`, `GetActuator`, or `RemoveActuator` is called with a `JointId` that does not exist
- **THEN** a `std::out_of_range` is thrown

### Requirement: Actuators apply joint torques automatically each step

On every `Step`, `PhysicsApp` SHALL, for each registered actuator: read the joint state, compute `τ = ComputeTorque(angle, angular_velocity)`, and apply the torque to the physics engine as equal-and-opposite world-space torques about the joint axis: `+τ·axis_world` on the child body and `−τ·axis_world` on the parent body, through the existing body external-torque write path.

Body torques SHALL accumulate across actuators rather than assign: a body shared by two actuated joints (a serial chain — one joint's child is the next joint's parent) receives the SUM of both joints' equal-and-opposite contributions. Whichever actuator is processed later SHALL NOT erase the earlier one's contribution, so a chained multi-link limb can hold and drive all of its joints.

The torques SHALL be applied on the same step's physics solve (written before the GPU upload). The sign convention SHALL be: positive `τ` rotates the child about the positive hinge axis relative to the parent.

Actuator contributions join the round's external-torque values like any other write of that round and are consumed by the same step. On an actuated joint's bodies, actuator contributions are written last within the step and accumulate with any user writes that landed on those slots earlier in the round.

#### Scenario: Torques written each step

- **WHEN** an actuator with a non-zero error is registered and `Step` is called
- **THEN** the child and parent bodies receive `+τ·axis_world` and `−τ·axis_world` respectively in the step's solve

#### Scenario: Chained joints accumulate on the shared body

- **WHEN** two actuated hinge joints share a middle body (the first joint's child is the second joint's parent) and both compute non-zero torques
- **THEN** the middle body receives the sum of both joints' contributions in the step's solve, and both joints reach their targets
