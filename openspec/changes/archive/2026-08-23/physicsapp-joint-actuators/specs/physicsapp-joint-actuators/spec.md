# physicsapp-joint-actuators

## Purpose

Give `PhysicsApp` joint-level identity and actuation: a `JointId` registry covering both URDF-imported and manually added hinge joints, an extensible `Actuator` base class with PD and DCMotor specializations, target-angle setting, joint-state readback, and automatic per-step torque application to the physics engine.

## ADDED Requirements

### Requirement: PhysicsApp assigns a JointId to every hinge joint

`PhysicsApp` SHALL assign a unique `JointId` (opaque `uint32_t`) to every physically realized hinge joint, whether created manually or imported from a URDF.

`AddHingeJoint(BodyId obj1, BodyId obj2, const HingeJointParams&)` SHALL return the assigned `JointId` (Building phase only). Calling it twice with the same `obj1` SHALL create two distinct joints (the engine supports multiple constraints per body).

`UrdfImportResult.joint_bodies` SHALL map joint name → `JointBodyPair { parent, child, id }` where `id` is the assigned `JointId`, for every joint that produced a physical constraint. `JointBodyPair` SHALL keep its existing `parent`/`child` semantics.

Calling `LoadUrdf` multiple times SHALL append joints to the same registry so every joint across all robots has a distinct `JointId`.

`PhysicsApp` SHALL keep per-joint metadata internally: parent `BodyId`, child `BodyId`, the hinge axis in the parent GO frame, and the initial relative rotation of the joint at scene commit.

#### Scenario: Manual hinge returns a JointId
- **WHEN** `AddHingeJoint` is called with two existing bodies
- **THEN** it returns a `JointId` valid for later actuator/target/readback calls

#### Scenario: URDF joints carry JointIds
- **WHEN** `LoadUrdf` returns for `a1.urdf`
- **THEN** `joint_bodies["FR_thigh_joint"].id` is a valid `JointId`
- **AND** `joint_bodies["FR_thigh_joint"].parent` and `.child` still equal the corresponding `link_bodies` values

#### Scenario: Multiple joints share a body
- **WHEN** two `AddHingeJoint` calls use the same `obj1`
- **THEN** both joints exist with distinct `JointId`s and each drives its own constraint

#### Scenario: Repeated URDF loads keep distinct JointIds
- **WHEN** `LoadUrdf` is called twice
- **THEN** every `JointId` from the second result differs from every `JointId` from the first

### Requirement: Actuator base class with PD and DCMotor specializations

`PhysicsApp` SHALL expose an `Actuator` base class (AppPhysics namespace) with:
- a virtual `float ComputeTorque(float angle, float angular_velocity) const` pure function,
- a non-virtual `SetTargetAngle(float)` / `float GetTargetAngle()` pair storing the target in the base class,
- a virtual destructor.

`PdActuator` SHALL compute `τ = kp·wrap(target − angle) + kd·(0 − angular_velocity)` where `wrap` maps the angle error into `[−π, π)`. Its constructor SHALL accept `kp` and `kd` with defaults `kp = 25.0f`, `kd = 0.5f`.

`DcMotorActuator` SHALL compute the same PD core, then clip the torque with the four-quadrant DC-motor torque-speed envelope: torque bound at the current joint velocity `ω` is derived from `stall_torque`, `no_load_speed`, `cont_torque`, `gear_ratio` (joint velocity scaled to the motor side by `gear_ratio`), and the result is bounded by `±cont_torque`. Its constructor SHALL accept all six parameters with defaults `kp = 25.0f`, `kd = 0.5f`, `stall_torque = 33.5f`, `no_load_speed = 21.0f`, `cont_torque = 13.4f`, `gear_ratio = 1.0f`.

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

### Requirement: Actuators are registered per joint in the Building phase

`PhysicsApp` SHALL provide `void AddActuator(JointId joint, std::unique_ptr<Actuator> actuator)` callable only during the Building phase (before `CommitScene`).

At most one actuator SHALL exist per joint: calling `AddActuator` on a joint that already has one SHALL throw `std::invalid_argument`. Passing a null actuator SHALL throw `std::invalid_argument`. An invalid `JointId` SHALL throw `std::out_of_range`. Calling after `CommitScene` SHALL throw `std::logic_error`.

`PhysicsApp` SHALL take ownership of the actuator; destruction SHALL be handled by the virtual destructor.

#### Scenario: Register a DCMotor actuator on a URDF joint
- **WHEN** `AddActuator` is called with a `JointId` from `LoadUrdf` and a `std::make_unique<DcMotorActuator>()`
- **THEN** the joint is driven by the actuator on subsequent `Step` calls

#### Scenario: Duplicate actuator rejected
- **WHEN** `AddActuator` is called twice for the same joint
- **THEN** the second call throws `std::invalid_argument`

#### Scenario: AddActuator after commit rejected
- **WHEN** `AddActuator` is called after `CommitScene`
- **THEN** it throws `std::logic_error`

### Requirement: Target angles are set per joint in either phase

`PhysicsApp` SHALL provide `void SetTargetAngle(JointId joint, float target)` legal in both the Building phase (pre-setting initial targets) and the Drive phase.

The target SHALL be interpreted as the desired joint angle **relative to the robot's loaded initial pose** (the angle at load is defined as 0). An invalid `JointId` SHALL throw `std::out_of_range`. Calling on a joint without an actuator SHALL throw `std::logic_error`.

#### Scenario: Target set before commit
- **WHEN** `SetTargetAngle` is called on an actuated joint before `CommitScene`
- **THEN** no exception is thrown and the target takes effect from the first `Step`

#### Scenario: Target on non-actuated joint rejected
- **WHEN** `SetTargetAngle` is called on a joint with no actuator
- **THEN** it throws `std::logic_error`

### Requirement: Joint state is readable after commit

`PhysicsApp` SHALL provide `struct JointState { float angle; float angular_velocity; }` and `JointState GetJointState(JointId joint) const`, legal only in the Drive phase (after `CommitScene`), returning the joint's signed angle (relative to the loaded initial pose, from the parent/child body rotations about the hinge axis) and angular velocity (relative angular velocity projected on the world axis).

Calling before `CommitScene` SHALL throw `std::logic_error`. An invalid `JointId` SHALL throw `std::out_of_range`.

#### Scenario: Initial angle is zero
- **WHEN** `GetJointState` is called on a joint immediately after `CommitScene` before any `Step`
- **THEN** the reported angle is approximately 0

#### Scenario: Driven joint angle tracks the target
- **WHEN** an actuator holds a target and enough `Step` calls run
- **THEN** `GetJointState` reports an angle converging toward the target

### Requirement: Actuators apply joint torques automatically each step

On every `Step`, `PhysicsApp` SHALL, for each registered actuator: read the joint state, compute `τ = ComputeTorque(angle, angular_velocity)`, and apply the torque to the physics engine as equal-and-opposite world-space torques about the joint axis: `+τ·axis_world` on the child body and `−τ·axis_world` on the parent body, through the existing body external-torque write path.

The torques SHALL be applied on the same step's physics solve (written before the GPU upload). The sign convention SHALL be: positive `τ` rotates the child about the positive hinge axis relative to the parent.

The torque channel SHALL be owned by the actuators: user writes of `ExternalTorque` on an actuated joint's bodies are overwritten each `Step`.

#### Scenario: Torques written each step
- **WHEN** an actuator with a non-zero error is registered and `Step` is called
- **THEN** the child and parent bodies receive `+τ·axis_world` and `−τ·axis_world` respectively in the step's solve

### Requirement: URDF joint limits are recorded but not enforced

For URDF joints, `PhysicsApp` SHALL record the parsed `limit_lower`, `limit_upper`, `limit_effort`, and `limit_velocity` as data on the joint (available to the caller), but SHALL NOT enforce them: neither targets nor computed torques are clipped to these limits, and the physics engine imposes no joint-angle limits.

#### Scenario: Limits recorded after URDF load
- **WHEN** a joint is imported from a URDF that declares limits
- **THEN** the limits are available from `PhysicsApp` for the joint
- **AND** a target outside the declared range is accepted without error

### Requirement: Standing test runs in windowed mode with tunable parameters

`PhysicsApp` SHALL ship a windowed standing test: a ground plane, the A1 robot, 12 `DcMotorActuator`s on the 12 leg joints, and targets from `DEFAULT_JOINT_POS` (hip ±0.1, thigh 0.8/1.0, calf −1.5). All actuator parameters and targets SHALL be constants at the top of the test file for manual tuning. The test SHALL log each joint's target/current angle/error every K steps.

#### Scenario: Standing test runs as a ctest smoke test
- **WHEN** the standing test is executed under ctest with a frame count
- **THEN** it runs that many frames without crashing and exits

#### Scenario: Standing test runs interactively
- **WHEN** the standing test is run without arguments
- **THEN** it opens a window, starts paused, resumes on SPACE, and logs joint angles periodically until the window closes

### Requirement: Control laws are unit-tested without a physics scene

`PhysicsApp` SHALL ship numeric unit tests that construct `PdActuator` and `DcMotorActuator` directly, feed known `(angle, angular_velocity)` values, and assert `ComputeTorque` results — including PD law values, angle-error wrapping, and DCMotor envelope clipping at zero speed, high speed, and `±cont_torque` bounds.

#### Scenario: All control-law assertions pass
- **WHEN** the actuator unit test runs
- **THEN** every assertion on PD and DCMotor outputs (including envelope edges) passes
