# physicsapp-joint-actuators

## MODIFIED Requirements

### Requirement: Actuators are registered per joint in the Building phase

`PhysicsApp` SHALL provide `void AddActuator(JointId joint, std::shared_ptr<Actuator> actuator)` callable only during the Building phase (before `CommitScene`).

At most one actuator SHALL exist per joint: calling `AddActuator` on a joint that already has one SHALL throw `std::invalid_argument`. Passing a null actuator SHALL throw `std::invalid_argument`. An invalid `JointId` SHALL throw `std::out_of_range`. Calling after `CommitScene` SHALL throw `std::logic_error`.

`PhysicsApp` SHALL retain ownership of the actuator through a `std::shared_ptr`; the actuator SHALL be kept alive as long as `PhysicsApp` holds it, and destruction SHALL be handled by the virtual destructor. The `shared_ptr` form SHALL allow the caller to keep additional references (e.g. a Python-bound trampoline object) that remain valid after the app releases its reference.

#### Scenario: Register a DCMotor actuator on a URDF joint
- **WHEN** `AddActuator` is called with a `JointId` from `LoadUrdf` and a `std::make_shared<DcMotorActuator>()`
- **THEN** the joint is driven by the actuator on subsequent `Step` calls

#### Scenario: Duplicate actuator rejected
- **WHEN** `AddActuator` is called twice for the same joint
- **THEN** the second call throws `std::invalid_argument`

#### Scenario: AddActuator after commit rejected
- **WHEN** `AddActuator` is called after `CommitScene`
- **THEN** it throws `std::logic_error`

#### Scenario: Caller-held reference outlives the app's
- **WHEN** a caller keeps its own `std::shared_ptr<Actuator>` after the `PhysicsApp` that registered it is destroyed
- **THEN** the caller's reference remains valid and usable
