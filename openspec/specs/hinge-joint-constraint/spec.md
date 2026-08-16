# hinge-joint-constraint

## Purpose

Define the complete hinge joint constraint system: simplified user-facing API with obj1-local-only parameters, GPU data layout using initial relative transform, COM-local conversion deferred to the physics flush, shader-side obj2-local value derivation, and unified naming convention (`hinge_axis` / `hinge_anchor`) across the full stack.

## Requirements

### Requirement: HingeJointDef stores only obj1-local parameters

The `HingeJointDef` struct SHALL contain:
- `ObjectHandle m_obj2_handle` — handle of the second object
- `glm::vec3 m_hinge_axis_obj1` — hinge axis direction in obj1's GO-local coordinate system
- `glm::vec3 m_hinge_anchor_obj1` — hinge anchor point in obj1's GO-local coordinate system (offset from obj1 GO origin)
- `float m_compliance` — joint compliance (default 0.0 for hard constraint)

The obj1 is implicitly the GameObject owning the `PhysicsConstraintComponent`. The struct SHALL NOT contain `m_obj2_local_aligned_axis` or `m_obj2_local_attach_point` fields. Component-level fields are expressed in GO-local space. Conversion to COM-local space SHALL be performed by `PhysicsAdaptor` during `Flush` (see `JointConverter`).

#### Scenario: HingeJointDef default construction

- **WHEN** a `HingeJointDef` is default-constructed
- **THEN** `m_obj2_handle` is invalid, all vec3 fields are zero vectors, and `m_compliance` is 0.0f

#### Scenario: User creates a hinge joint definition

- **WHEN** a user constructs a `HingeJointDef` with `m_hinge_axis_obj1 = (0, 1, 0)` and `m_hinge_anchor_obj1 = (0, 0, 0)`
- **THEN** no obj2-local values are required from the user
- **AND** the system derives COM-local values from GO-local values and COM offsets during `PhysicsAdaptor::Flush`

### Requirement: Init computes initial relative transform and submits hinge joints

`PhysicsConstraintComponent::Awake()` SHALL, for each joint definition, allocate a joint slot via `PhysicsAdaptor::AllocateHingeJoint()` and record the allocated index. It SHALL NOT resolve handles or compute transforms.

`PhysicsConstraintComponent::Init()` SHALL, for each `HingeJointDef`:
1. Resolve the obj2 handle to a rigid body index via `PhysicsAdaptor::FindRigidBodyByObjectHandle`
2. Read obj1's and obj2's world transforms
3. Normalize `m_hinge_axis_obj1`; if the normalized length is below `1e-6`, log an error and skip the joint
4. Compute `initial_rel_rotation = glm::inverse(q1) * q2` and `initial_rel_pos_local = glm::inverse(q1) * (pos2 - pos1)` (position before rotation, consistent with fixed joints)
5. Submit via `PhysicsAdaptor::SubmitHingeJoint(joint_idx, data)` with all values in GO-local space

#### Scenario: Hinge joint Init computes initial relative transform

- **WHEN** a `HingeJointDef` is processed during `Init()` with obj1 at position (0,0,0), rotation identity and obj2 at position (1,0,0), rotation identity
- **THEN** `initial_rel_rotation` is identity and `initial_rel_pos_local` is (1,0,0)
- **AND** these values are submitted to the adaptor in GO-local space

#### Scenario: Hinge axis is validated and normalized

- **WHEN** `Init()` processes a `HingeJointDef` with `m_hinge_axis_obj1 = (0, 2, 0)`
- **THEN** the axis is normalized to (0, 1, 0) before submission

#### Scenario: Zero-length hinge axis is rejected

- **WHEN** `Init()` processes a `HingeJointDef` with `m_hinge_axis_obj1` whose normalized length is below 1e-6
- **THEN** an `SDL_LogError` is emitted and the hinge joint is not submitted

### Requirement: HingeJointComDescriptor stores initial relative transform (COM-local)

The `HingeJointComDescriptor` struct (std430 compatible, 80 bytes, formerly named `GpuHingeJoint`) SHALL be defined in `engine/Physics/PhysicsDescriptors.h` and SHALL contain:
- `uint32_t obj1_index`, `uint32_t obj2_index`, `float compliance`, `float _pad`
- `glm::vec4 hinge_axis_obj1` — hinge axis in obj1's COM-local frame
- `glm::vec4 hinge_anchor_obj1` — hinge anchor in obj1's COM-local frame
- `glm::vec4 initial_rel_pos_local` — `q1_com_init⁻¹ * (pos2_com_init - pos1_com_init)` in COM-local space
- `glm::vec4 initial_rel_rotation` — `q1_com_init⁻¹ * q2_com_init` as a quaternion (xyzw)

The struct SHALL NOT contain `obj2_local_aligned_axis`, `obj2_local_attach_point`, `obj1_local_aligned_axis`, or `obj1_local_attach_point` fields. The struct SHALL be declared with `PHYSICS_API`.

#### Scenario: HingeJointComDescriptor size remains 80 bytes

- **WHEN** `sizeof(HingeJointComDescriptor)` is evaluated in C++
- **THEN** the size is 80 bytes (5 × vec4 equivalent)

#### Scenario: Fields match between C++ and GLSL

- **WHEN** the C++ `HingeJointComDescriptor` struct and the GLSL `HingeJointComDescriptor` struct are compared
- **THEN** field names, types, and ordering match for std430 compatibility

### Requirement: Joint registration uses Allocate + Submit pattern

`PhysicsScene::RegisterHingeJoint()` SHALL NOT exist. Joint registration SHALL use the Allocate + Submit pattern:

- `PhysicsAdaptor::AllocateHingeJoint()` (via `PhysicsConstraintComponent::Awake`) reserves a slot and returns its index
- `PhysicsAdaptor::SubmitHingeJoint(uint32_t, const HingeJointSubmitData &)` (via `PhysicsConstraintComponent::Init`) stores the GO-local submission data in `m_pending_hinge_joints`
- `PhysicsAdaptor::Flush` converts pending submissions to COM-local `HingeJointComDescriptor` values via `JointConverter::ConvertHinge` and calls `PhysicsScene::SubmitHingeJoint(uint32_t, const HingeJointComDescriptor &)` to write the slot

#### Scenario: PhysicsScene SubmitHingeJoint fills a pre-allocated slot

- **WHEN** `AllocateHingeJoint()` returns index `j`, and `SubmitHingeJoint(j, joint)` is called with a converted `HingeJointComDescriptor`
- **THEN** slot `j` in `m_hinge_joints` is populated with the provided values

### Requirement: Shader derives obj2-local values from initial relative transform

The `accumulate_hinge_position.comp` shader SHALL, before computing the aligned-axis and attachment-point constraints, derive obj2-local hinge axis and anchor from the stored initial relative transform:

- `obj2_local_axis = quat_rotate(quat_inverse(initial_rel_rotation), hinge_axis_obj1.xyz)`
- `obj2_local_anchor = quat_rotate(quat_inverse(initial_rel_rotation), hinge_anchor_obj1.xyz - initial_rel_pos_local.xyz)`

The derived local variables SHALL be used in place of removed struct fields `obj2_local_aligned_axis` and `obj2_local_attach_point`. The constraint solving logic (cross-product for axis alignment, position difference for anchor coincidence) SHALL remain unchanged.

#### Scenario: Constraint starts satisfied at initialization

- **WHEN** the shader processes a hinge joint on the first frame after registration, using both bodies' initial world transforms
- **THEN** `cross(axis1_world, axis2_world)` yields zero and `attach2_world - attach1_world` yields zero
- **AND** no correction impulses are applied

#### Scenario: Bodies rotate away from each other

- **WHEN** obj2 rotates so its hinge axis is no longer parallel to obj1's hinge axis
- **THEN** the aligned-axis constraint produces a non-zero correction
- **AND** angular impulses are applied to both bodies

#### Scenario: Bodies translate away from anchor point

- **WHEN** obj2 translates so its anchor point no longer coincides with obj1's anchor point
- **THEN** the attachment-point constraint produces a non-zero correction
- **AND** linear and angular impulses are applied to both bodies

### Requirement: Full-stack naming convention uses hinge_axis and hinge_anchor

All identifiers across the codebase SHALL use `hinge_axis` and `hinge_anchor` in place of `aligned_axis` / `AlignedAxis` and `attach_point` / `AttachPoint`. This includes:

- CPU: `HingeJointComDescriptor` fields, `HingeJointDef` fields, `HingeJointSubmitData` fields
- Shader structs: field names in `accumulate_hinge_position.comp`
- Shader buffers: `HingeAxisLagrange` and `HingeAnchorLagrange` (replacing `HingeAlignedAxisLagrange` and `HingePositionLagrange`)
- Shader accessors: `hinge_axis_lagrange` and `hinge_anchor_lagrange`
- Solver C++: `gpu_hinge_axis_lagrange` and `gpu_hinge_anchor_lagrange` buffer member names
- Solver debug names: `"XPBD HingeAxisLagrange"` and `"XPBD HingeAnchorLagrange"`
- Clear shader: `clear_hinge_lagrange.comp` buffer binding and accessor names

The file name `clear_hinge_lagrange.comp` SHALL remain unchanged.

#### Scenario: Shader reads new buffer names

- **WHEN** the solver dispatches the accumulate hinge position shader
- **THEN** the shader binds to `HingeAxisLagrange` and `HingeAnchorLagrange` buffers
- **AND** no reference to `HingeAlignedAxisLagrange` or `HingePositionLagrange` exists

#### Scenario: Solver allocates buffers with new names

- **WHEN** `EnsureIntermediateBuffers()` creates Lagrange multiplier buffers
- **THEN** hinge buffers are named `gpu_hinge_axis_lagrange` and `gpu_hinge_anchor_lagrange` in C++
- **AND** debug names are `"XPBD HingeAxisLagrange"` and `"XPBD HingeAnchorLagrange"`
