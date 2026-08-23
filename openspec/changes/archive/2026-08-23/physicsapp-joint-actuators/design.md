# Design: PhysicsApp Joint Actuators

## Context

`PhysicsApp` (app/physics, DLL) exposes a Building phase → `CommitScene` → Drive phase lifecycle. `LoadUrdf` returns `UrdfImportResult` with `link_bodies` and `joint_bodies` (name → `{parent, child} BodyId`), but no joint axis or angle. The physics engine (XPBD GPU solver) provides per-body world-space `ExternalTorque` writes via `SetBodyValue`, and per-body state readback (`GetBodyState`/`GetBodyStates`). There is no joint-angle readback and no joint-torque input at the engine level.

Key engine facts established during exploration:

- The GPU hinge constraint (axis alignment + anchor point) is defined by `HingeJointComDescriptor` (axis in obj1 COM frame + `initial_rel_rotation`), but `JointConverter::ConvertHinge` passes the axis and initial relative rotation through **translation-only**: the engine implicitly treats the COM frame orientation as equal to the GO frame (`PhysicsAdaptor.cpp` → `JointConverter.hpp:63-86`). So CPU-side angle math using the GO-frame axis + body readback rotations is exactly consistent with the GPU constraint.
- `PhysicsConstraintComponent` holds a vector of joint definitions (`m_joints`), and `Scene::AddComponent` does not deduplicate by type — one body can already host multiple hinge constraints mechanically (two components, or one component with several defs).
- `PhysicsApp::Step()` runs: pre-wait → `RecordBodyStateUpload` (applies pending `SetBodyValue` writes) → `GPUStep` → `RecordBodyStateCopy` (readback to CPU staging) → post-wait. Actuators slot in between readback and upload.

## Goals / Non-Goals

**Goals:**

- Unified `JointId` identity covering both URDF-imported joints and manually added hinge joints.
- CPU-side joint angle/angular-velocity computation consistent with the GPU hinge convention.
- `Actuator` base class with `PdActuator` and `DcMotorActuator` specializations, extensible without touching `PhysicsApp`'s API.
- Per-step automatic torque application through the existing `ExternalTorque` body write path.
- Windowed standing test (user-tunable) + numeric control-law unit tests.

**Non-Goals:**

- Engine solver changes: no GPU hinge motor, no joint-angle GPU readback, no joint-limit constraint.
- Enforcing URDF joint limits (recorded as data only).
- A separate `ActuatorId`: at most one actuator per joint, so `ActuatorId == JointId`.
- Batch/name-keyed target APIs (`SetTargetAngle` takes `JointId` only; name collisions across multiple URDF imports make name APIs fragile).
- Exposing `SetJointTorque` publicly (torque channel is owned by actuators).
- Ground-contact standing stability: the standing test is a tunable harness, not an automated stability assertion.

## Decisions

### D1. App-level JointId registry; no engine identity changes

`PhysicsApp::Impl` owns `std::vector<JointRecord>`; `JointId` is the index. `JointRecord = { optional name, parent BodyId, child BodyId, glm::vec3 axis (parent GO frame), glm::quat initial_rel_rotation, optional URDF limits }`.

- `AddHingeJoint(BodyId, BodyId, params)` changes return type `void → JointId` (source-compatible for existing callers; return type is not part of the mangled name).
- `UrdfImportResult.joint_bodies` value `JointBodyPair` gains `JointId id` (plus the existing parent/child).
- The app is the sole creator of every joint (manual and URDF paths both flow through `PhysicsApp`), so metadata is recorded at creation time; **no reverse lookup from GOs/components is ever needed**.

*Alternatives considered:*
- Dedicated joint GameObjects carrying `PhysicsConstraintComponent` (gives joints first-class scene entities). Rejected for this iteration: requires an `m_obj1_handle` override in `HingeJointDef` (dedicated GOs have no `RigidBodyComponent`), adds hierarchy noise, and its benefits (joint limits, editor visualization, GPU readback) are future work — YAGNI.
- Engine-side joint ID in `PhysicsConstraintComponent`/adaptor. Rejected: the adaptor's slot indices already exist; exposing them would couple the app to component internals with no added value.

### D2. Axis source: extend `UrdfBuiltJoint`, reuse params for manual joints

- Manual joints: `params.axis_obj1` (normalized) is the axis in parent GO frame — the app already has it.
- URDF joints: `UrdfLoader` already computes `UrdfAxisToEngine(joint.axis)` at `UrdfLoader.cpp:489`; add `glm::vec3 axis{}` to `UrdfBuiltJoint` (Framework) and fill it there. The app copies it into `JointRecord` — no axis-conversion duplication.
- `initial_rel_rotation = q_parent⁻¹ · q_child` is computed at `CommitScene` (after `FlushCmdQueue`, when GO world transforms are final) using `SceneBuilder::GetBodyGameObject` + `GetWorldTransform` — the same values `PhysicsConstraintComponent::Init` used.

### D3. Joint angle math (CPU, consistent with GPU)

```
R0      = q_parent⁻¹ · q_child                              // recorded at CommitScene
q_rel   = inv(R_parent) · R_child · inv(R0)                 // deviation from loaded pose
q       = 2·atan2( dot(axis, imag(q_rel)), real(q_rel) )    // q = 0 at load; atan2(0, -1) guard
ω       = dot(ω_child − ω_parent, axis_world)               // axis_world = R_parent · axis
```

Torque application (per step, world space, about each body's COM):

```
τ on child  = +τ · axis_world   (ExternalTorque)
τ on parent = −τ · axis_world   (ExternalTorque)
```

Positive τ rotates the child about +axis relative to the parent (URDF convention).

### D4. `Actuator` base class — scalar control law, no physics awareness

```cpp
// app/physics/Actuator.h (AppPhysics namespace)
class Actuator {
public:
    virtual ~Actuator() = default;
    virtual float ComputeTorque(float angle, float angular_velocity) const = 0;
    void SetTargetAngle(float target);
    float GetTargetAngle() const;
private:
    float m_target_angle{0.0f};
};

class PdActuator : public Actuator {
public:
    explicit PdActuator(float kp = 25.0f, float kd = 0.5f);
    float ComputeTorque(float angle, float angular_velocity) const override;
private:
    float m_kp, m_kd;
};

class DcMotorActuator : public Actuator {
public:
    explicit DcMotorActuator(float kp = 25.0f, float kd = 0.5f,
                             float stall_torque = 33.5f, float no_load_speed = 21.0f,
                             float cont_torque = 13.4f, float gear_ratio = 1.0f);
    float ComputeTorque(float angle, float angular_velocity) const override;
private:
    float m_kp, m_kd, m_stall_torque, m_no_load_speed, m_cont_torque, m_gear_ratio, m_corner_speed;
};
```

- PD: `τ = kp·clamp_angle(target − angle) + kd·(0 − ω)`.
- DCMotor: same PD core, then the four-quadrant torque-speed envelope from the reference implementation:
  - `ω_motor = ω / gear_ratio`, `ω_max_motor = no_load_speed / gear_ratio`, `corner_speed = no_load_speed·(1 + cont_torque/stall_torque)`
  - upper = `stall_torque·(1 − ω_clip/ω_max_motor)`, lower = `stall_torque·(−1 − ω_clip/ω_max_motor)`, clipped to `±cont_torque`.
- `ComputeTorque` is `const`, no per-step mutable state; `clamp_angle` wraps the angle error to `[−π, π)` (pure math, matches the reference; harmless for limited revolute joints).
- Constructors take plain parameters with defaults; future actuators that need a time step receive it in their constructor (`dt` is not part of `ComputeTorque`). Defaults: kp=25, kd=0.5, stall=33.5 N·m, no-load=21.0 rad/s, cont=13.4 N·m, gear=1.0 (Unitree A1 joint-side values).

*Alternatives considered:*
- Tagged structs / `std::variant` params. Rejected per user preference: plain constructor arguments with defaults; `DcMotorActuator{}` is the A1 configuration.
- `ComputeTorque(angle, omega, dt)` signature. Rejected: neither actuator uses dt; the app has no dt source without reaching into solver config. The constructor-parameter route keeps the virtual surface minimal and future-proof.
- Actuators knowing about bodies/joints. Rejected: keeping the control law a pure scalar function decouples it from physics I/O and enables direct numeric testing.

### D5. Registration and drive API

```cpp
// Building phase only
void AddActuator(JointId joint, std::unique_ptr<Actuator> actuator);

// Both phases
void SetTargetAngle(JointId joint, float target);

// Drive phase readback
struct JointState { float angle; float angular_velocity; };
JointState GetJointState(JointId joint) const;
```

Error contract (all throw `std::logic_error` after `CommitScene` where noted):

| Call | Condition | Exception |
|---|---|---|
| `AddActuator` | after `CommitScene` | `std::logic_error` |
| `AddActuator` | invalid `JointId` | `std::out_of_range` |
| `AddActuator` | `actuator == nullptr` | `std::invalid_argument` |
| `AddActuator` | joint already has actuator | `std::invalid_argument` |
| `SetTargetAngle` | invalid `JointId` | `std::out_of_range` |
| `SetTargetAngle` | no actuator on joint | `std::logic_error` |
| `GetJointState` | before `CommitScene` | `std::logic_error` |
| `GetJointState` | invalid `JointId` | `std::out_of_range` |

Ownership: `unique_ptr<Actuator>` transfers into the app; virtual destructor handles cross-DLL destruction (same documented pattern as exceptions crossing the DLL boundary).

### D6. Step integration

In `Step()`, between readback availability and `RecordBodyStateUpload`:

```
for each actuator:
    state = read joint state (angle, omega from CPU staging + JointRecord)
    tau   = actuator->ComputeTorque(angle, omega)
    write ExternalTorque(child, +tau·axis_world), ExternalTorque(parent, −tau·axis_world)
```

`ExternalTorque` is caller-managed and persists, so actuators overwrite it every step — no clearing needed. Torques are applied on the next `GPUStep`.

### D7. Tests

**Standing test** (`physics_app_actuator_stand_test.cpp`, Windowed): ground plane (kinematic box, mass 0), A1 URDF loaded, 12 `DcMotorActuator`s, targets from `DEFAULT_JOINT_POS`, tunable constants at file top, periodic `SDL_LogInfo` (every K steps: joint name / target / current angle / error). Registered with `anro_add_test(... WINDOWED FRAMES 600 TIMEOUT 300 ...)` → smoke test under ctest; manual run (no arg) starts paused, SPACE resumes, user tunes kp/kd to make it stand.

**Numeric control-law tests** (`physics_app_actuator_unit_test.cpp`, no physics scene): construct `PdActuator`/`DcMotorActuator`, feed known `(q, ω)`, assert `ComputeTorque` — includes DCMotor envelope edges: zero-speed → stall clip at `±cont_torque`/`±stall`; high joint speed → sign-reversed torque segment; angle wrap across `±π`.

## Risks / Trade-offs

- **CPU-side angle vs GPU constraint drift** → Both use the same axis/R0 conventions (translation-only COM conversion, D3); the standing test exercises the full loop, and `GetJointState` output is observable via the tuning log.
- **Explicit torques vs solver hard constraints** → Torque about the hinge axis is orthogonal to the axis-alignment and anchor constraints, so it passes through cleanly (standard joint-torque mode). If instability appears with stiff gains, mitigation is parameter tuning (kp/kd), not engine changes.
- **Actuator overwrites user `ExternalTorque` writes on the same bodies** → Documented warning; users wanting custom force/torque control should not use the same bodies with actuators, or should register no actuator on the affected joints.
- **Kinematic parent/child joints** → Solver skips both-kinematic joints; actuators still compute and write torque, but it has no effect. Documented, no error raised.
- **`AddHingeJoint` return-type change** → Source-compatible for callers that ignore the return; binary unchanged (return type not in the mangled name).
- **R0 timing** → Computed at `CommitScene` after `FlushCmdQueue`; verify world transforms are final at that point during implementation (same point the engine's `PhysicsConstraintComponent::Init` reads them).

## Migration Plan

No migration. `UrdfBuiltJoint` gains one field (default-initialized); `AddHingeJoint` return change is source-compatible; existing tests keep passing. No solver, buffer, or adaptor changes.

## Open Questions

- None blocking. R0 timing (above) is verified during implementation.
