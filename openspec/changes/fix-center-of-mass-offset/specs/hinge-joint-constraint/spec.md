# hinge-joint-constraint Delta Spec

## MODIFIED Requirements

### Requirement: HingeJointDef stores only obj1-local parameters

The `HingeJointDef` struct SHALL contain:
- `ObjectHandle m_obj2_handle` — handle of the second object
- `glm::vec3 m_hinge_axis_obj1` — hinge axis direction in obj1's GO-local coordinate system
- `glm::vec3 m_hinge_anchor_obj1` — hinge anchor point in obj1's GO-local coordinate system (offset from obj1 GO origin)
- `float m_compliance` — joint compliance (default 0.0 for hard constraint)

The obj1 is implicitly the GameObject owning the `PhysicsConstraintComponent`. The struct SHALL NOT contain `m_obj2_local_aligned_axis` or `m_obj2_local_attach_point` fields. Component-level fields are expressed in GO-local space. Conversion to COM-local space SHALL be performed by `PhysicsScene` during `InitializePendingRigidBodies`.

#### Scenario: HingeJointDef default construction

- **WHEN** a `HingeJointDef` is default-constructed
- **THEN** `m_obj2_handle` is invalid, all vec3 fields are zero vectors, and `m_compliance` is 0.0f

#### Scenario: User creates a hinge joint definition

- **WHEN** a user constructs a `HingeJointDef` with `m_hinge_axis_obj1 = (0, 1, 0)` and `m_hinge_anchor_obj1 = (0, 0, 0)`
- **THEN** no obj2-local values are required from the user
- **AND** the system derives COM-local values from GO-local values and COM offsets during `InitializePendingRigidBodies`

### Requirement: GpuHingeJoint stores initial relative transform (COM-local)

The `GpuHingeJoint` struct (std430 compatible, 80 bytes) SHALL contain:
- `uint32_t obj1_index`, `uint32_t obj2_index`, `float compliance`, `float _pad`
- `glm::vec4 hinge_axis_obj1` — hinge axis in obj1's COM-local frame
- `glm::vec4 hinge_anchor_obj1` — hinge anchor in obj1's COM-local frame
- `glm::vec4 initial_rel_pos_local` — `q1_com_init⁻¹ * (pos2_com_init - pos1_com_init)` in COM-local space
- `glm::vec4 initial_rel_rotation` — `q1_com_init⁻¹ * q2_com_init` as a quaternion (xyzw)

The struct SHALL NOT contain `obj2_local_aligned_axis`, `obj2_local_attach_point`, `obj1_local_aligned_axis`, or `obj1_local_attach_point` fields.

#### Scenario: GpuHingeJoint size remains 80 bytes

- **WHEN** `sizeof(GpuHingeJoint)` is evaluated in C++
- **THEN** the size is 80 bytes (5 × vec4 equivalent)

#### Scenario: Fields match between C++ and GLSL

- **WHEN** the C++ `GpuHingeJoint` struct and the GLSL `GpuHingeJoint` struct are compared
- **THEN** field names, types, and ordering match for std430 compatibility

### Requirement: RegisterHingeJoint removed — UpdateHingeJoint is the sole path

`PhysicsScene::RegisterHingeJoint()` SHALL be removed. Joint registration SHALL use the Allocate + Update pattern: `AllocateHingeJoint()` reserves a slot during Awake, and `UpdateHingeJoint()` fills it with resolved data during Init.

`UpdateHingeJoint()` SHALL accept parameters in the order:
1. `uint32_t joint_idx`
2. `uint32_t obj1_index`, `uint32_t obj2_index`
3. `float compliance`
4. `const glm::vec3 &hinge_axis_obj1`
5. `const glm::vec3 &hinge_anchor_obj1`
6. `const glm::vec3 &initial_rel_pos_local`
7. `const glm::quat &initial_rel_rotation`

Parameters 4-7 are expressed in GO-local space when called from `PhysicsConstraintComponent::Init`. PhysicsScene SHALL convert them to COM-local space during `InitializePendingRigidBodies`.

#### Scenario: UpdateHingeJoint fills a pre-allocated slot

- **WHEN** `AllocateHingeJoint()` returns index `j`, and `UpdateHingeJoint(j, 0, 1, 0.0, axis, anchor, rel_pos, rel_rot)` is called
- **THEN** slot `j` in `m_hinge_joints` is populated with the provided values

## REMOVED Requirements

### Requirement: RegisterHingeJoint accepts initial relative transform

**Reason**: This method was unused (all callers use Allocate + Update). The Register + push_back pattern is incompatible with the deferring of COM conversion.

**Migration**: Use `AllocateHingeJoint()` + `UpdateHingeJoint()` pattern instead. Existing call sites already use this pattern and require no changes.
