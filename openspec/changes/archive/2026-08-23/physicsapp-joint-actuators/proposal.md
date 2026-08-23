# Proposal: PhysicsApp Joint Actuators

## Why

`PhysicsApp` can load A1 robots and read back body state, but there is no way to drive the robot: no joint angle readback, no joint torque input, and no control layer. The goal is to let a caller set target joint angles and have PD / DCMotor actuators convert them into hinge-axis torques applied automatically each step — the foundation for standing and locomotion experiments.

## What Changes

- **New** `JointId` identity in `PhysicsApp`: `AddHingeJoint` returns a `JointId` (signature `void → JointId`, source-compatible for callers that ignore it); `UrdfImportResult.joint_bodies` values gain the assigned `JointId`.
- **New** App-side joint registry (vector in `PhysicsApp::Impl`, `JointId = index`) recording per-joint `{parent BodyId, child BodyId, axis (parent GO frame), initial relative rotation R0, optional URDF limits}`. R0 is computed at `CommitScene` from GO world transforms, matching the engine's constraint initialization.
- **New** `Actuator` base class plus `PdActuator` and `DcMotorActuator` specializations in `app/physics/Actuator.h` (AppPhysics namespace, glm/std only). Pure scalar control law: `ComputeTorque(angle, angular_velocity)`, target angle held in the base class. Constructors take plain parameters with A1 defaults; future types that need a time step take it in their constructor.
- **New** `PhysicsApp::AddActuator(JointId, std::unique_ptr<Actuator>)` (Building phase): at most one actuator per joint; new actuator types require no API changes.
- **New** `PhysicsApp::SetTargetAngle(JointId, float)` (both phases): target angle is the rotation from the robot's loaded initial pose (q = 0 at load).
- **New** `PhysicsApp::GetJointState(JointId)` returning `{angle, angular_velocity}`, computed CPU-side from body readback, axis and R0 — same conventions as the GPU hinge constraint.
- **Modified** `PhysicsApp::Step()`: before the physics upload, actuators read joint state, compute torque, and apply `±τ·axis_world` to child/parent via the `ExternalTorque` write path.
- **Modified** `Engine::UrdfBuiltJoint`: gains an `axis` field (hinge axis already converted to the parent GO frame by `UrdfLoader`), so the app does not duplicate the axis-frame conversion.
- **New** windowed standing test (`physics_app_actuator_stand_test.cpp`): ground + A1 + 12 DCMotor actuators targeting `DEFAULT_JOINT_POS`, with tunable constants at the top and periodic joint-angle logging.
- **New** numeric control-law unit tests: `PdActuator` / `DcMotorActuator` fed known `(q, ω)` assert `ComputeTorque` output, including DCMotor four-quadrant envelope edges.
- No solver/GPU changes. Joint limits (URDF `limit_lower/upper/effort/velocity`) are recorded as data only, not enforced.

## Capabilities

### New Capabilities
- `physicsapp-joint-actuators`: `PhysicsApp` joint identity (`JointId` registry), the `Actuator` base class with PD/DCMotor specializations, registration, target setting, joint-state readback, per-step torque application, and the standing/numeric tests.

### Modified Capabilities
- `urdf-import`: `UrdfBuiltJoint` carries the engine-frame hinge axis so consumers of `BuildRobotScene` can compute joint angles without re-implementing the URDF axis conversion.

## Impact

- `app/physics/PhysicsApp.h/.cpp` — `JointId`, `JointState`, `UrdfImportResult`/`JointBodyPair` extension, `AddHingeJoint` return type, `AddActuator`, `SetTargetAngle`, `GetJointState`, Step integration, joint registry in `Impl`.
- `app/physics/Actuator.h/.cpp` — new files (auto-picked-up by the existing `file(GLOB_RECURSE)`).
- `app/physics/SceneBuilder.h/.cpp` — expose body GameObject access for R0 computation if needed; joint registry helpers.
- `engine/Framework/Import/UrdfTypes.h` + `UrdfLoader.cpp` — `UrdfBuiltJoint::axis` field (one line, already computed at build).
- `test/app/physics/physics_app_actuator_stand_test.cpp` — new windowed test.
- `test/app/physics/physics_app_actuator_unit_test.cpp` — new numeric control-law test.
- `test/app/physics/CMakeLists.txt` — register the two tests.
- No changes to the physics solver, GPU buffers, or the PhysicsAdaptor interface.
