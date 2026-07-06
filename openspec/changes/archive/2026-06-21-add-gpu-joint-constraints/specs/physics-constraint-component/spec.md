# physics-constraint-component

## Purpose

Define the CPU-side `PhysicsConstraintComponent` that stores joint definitions on a GameObject, validates referenced objects, resolves handles to rigid body indices, and registers joints with PhysicsScene.

## Requirements

### Requirement: Component stores arbitrary joint definitions

The `PhysicsConstraintComponent` SHALL store a collection of joint definitions, where each definition is one of two types: `FixedJointDef` or `HingeJointDef`. The collection size SHALL have no hardcoded upper bound.

#### Scenario: Component with multiple joints

- **WHEN** a `PhysicsConstraintComponent` has a `FixedJointDef` and a `HingeJointDef`
- **THEN** both are stored in the component's `m_joints` collection
- **AND** both are processed during `Awake()`

### Requirement: FixedJointDef fields

The `FixedJointDef` struct SHALL contain:
- `ObjectHandle m_obj2_handle` — id of the second object
- `float m_compliance` — compliance of the joint (default 0.0 for hard constraint)

The obj1 is implicitly the GameObject owning the `PhysicsConstraintComponent`. No attach points are stored on the definition; the initial relative transform between obj1 and obj2 is computed at `Awake()` time from the current world transforms.

#### Scenario: FixedJointDef default construction

- **WHEN** a `FixedJointDef` is default-constructed
- **THEN** `m_obj2_handle` is invalid and `m_compliance` is 0.0f

### Requirement: HingeJointDef fields

The `HingeJointDef` struct SHALL contain:
- `ObjectHandle m_obj2_handle` — id of the second object
- `glm::vec3 m_obj1_local_aligned_axis` — aligned axis in obj1's local coordinate system
- `glm::vec3 m_obj2_local_aligned_axis` — aligned axis in obj2's local coordinate system
- `glm::vec3 m_obj1_local_attach_point` — attachment point in obj1's local coordinate system
- `glm::vec3 m_obj2_local_attach_point` — attachment point in obj2's local coordinate system
- `float m_compliance` — compliance of the joint (default 0.0)

Angle limits and target angle fields SHALL NOT be present.

#### Scenario: HingeJointDef default construction

- **WHEN** a `HingeJointDef` is default-constructed
- **THEN** all vec3 fields are zero vectors and `m_compliance` is 0.0f

### Requirement: Component requires RigidBodyComponent on owner

The `PhysicsConstraintComponent::Awake()` SHALL verify that the owning GameObject has a `RigidBodyComponent`. If it does not, `Awake()` SHALL return without registering any joints.

#### Scenario: Owner has no RigidBodyComponent

- **WHEN** `Awake()` runs on a `PhysicsConstraintComponent` whose owner has no `RigidBodyComponent`
- **THEN** no joints are registered with PhysicsScene
- **AND** no error is logged (the component is simply inert)

### Requirement: Joint obj2 must have RigidBodyComponent

For each joint in the collection, `Awake()` SHALL find the RigidBodyComponent on the GameObject referenced by `m_obj2_handle`. If the obj2 GameObject does not exist or has no `RigidBodyComponent`, the joint SHALL be skipped with an error logged via `SDL_LogError`.

#### Scenario: obj2 missing RigidBodyComponent

- **WHEN** a joint's `m_obj2_handle` points to a GameObject without a `RigidBodyComponent`
- **THEN** an error message is logged identifying the joint type and the invalid handle
- **AND** that joint is not registered with PhysicsScene
- **AND** other valid joints in the same component are still registered

### Requirement: FixedJoint initial relative transform computation

For each FixedJointDef, `Awake()` SHALL compute the initial relative transform from the current world transforms:
- `initial_rel_pos_local = q1_inv.rotate(pos2_world - pos1_world)` — obj2 COM position in obj1's local frame
- `initial_rel_rotation = q1_inv * q2` — relative rotation in quaternion form

These values SHALL be stored as part of the joint registration data passed to PhysicsScene.

#### Scenario: FixedJoint snapshots initial pose at Awake

- **WHEN** `Awake()` processes a FixedJointDef
- **AND** both obj1 and obj2 have initialized world transforms
- **THEN** the initial relative position and rotation are computed from the current world state
- **AND** these values are included in the registration data

### Requirement: Joint registration with PhysicsScene

For each valid joint, `Awake()` SHALL call the appropriate registration method on `PhysicsScene`:
- `RegisterFixedJoint(obj1_index, obj2_index, compliance, initial_rel_pos_local, initial_rel_rotation, obj2_local_attach_point)` for FixedJoint
- `RegisterHingeJoint(obj1_index, obj2_index, compliance, ...)` for HingeJoint

#### Scenario: Valid joint registered successfully

- **WHEN** both obj1 and obj2 have valid rigid body indices
- **THEN** the joint data (including resolved indices) is passed to PhysicsScene
- **AND** the joint is queued for GPU buffer upload on the next `RefreshGpuBuffers()`
