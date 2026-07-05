# hinge-joint-constraint

## Purpose

Define the complete hinge joint constraint system: simplified user-facing API with obj1-local-only parameters, GPU data layout using initial relative transform, shader-side obj2-local value derivation, and unified naming convention (`hinge_axis` / `hinge_anchor`) across the full stack.

## Requirements

### Requirement: HingeJointDef stores only obj1-local parameters

The `HingeJointDef` struct SHALL contain:
- `ObjectHandle m_obj2_handle` — handle of the second object
- `glm::vec3 m_hinge_axis_obj1` — hinge axis direction in obj1's local coordinate system
- `glm::vec3 m_hinge_anchor_obj1` — hinge anchor point in obj1's local coordinate system (offset from obj1 COM)
- `float m_compliance` — joint compliance (default 0.0 for hard constraint)

The obj1 is implicitly the GameObject owning the `PhysicsConstraintComponent`. The struct SHALL NOT contain `m_obj2_local_aligned_axis` or `m_obj2_local_attach_point` fields. Obj2-local values SHALL be derived by the system at `Awake()` time and stored in the GPU buffer.

#### Scenario: HingeJointDef default construction

- **WHEN** a `HingeJointDef` is default-constructed
- **THEN** `m_obj2_handle` is invalid, all vec3 fields are zero vectors, and `m_compliance` is 0.0f

#### Scenario: User creates a hinge joint definition

- **WHEN** a user constructs a `HingeJointDef` with `m_hinge_axis_obj1 = (0, 1, 0)` and `m_hinge_anchor_obj1 = (0, 0, 0)`
- **THEN** no obj2-local values are required from the user
- **AND** the system derives obj2-local values from world transforms at `Awake()` time

### Requirement: Awake computes initial relative transform for hinge joints

`PhysicsConstraintComponent::Awake()` SHALL, for each `HingeJointDef`, resolve the obj2 handle to a GameObject, read its world transform, and compute `initial_rel_rotation = glm::inverse(q1) * q2` and `initial_rel_pos_local = glm::inverse(q1) * (pos2 - pos1)` using obj1's and obj2's world transforms. These values SHALL be passed to `RegisterHingeJoint()` alongside the user-provided `m_hinge_axis_obj1` and `m_hinge_anchor_obj1`.

#### Scenario: Hinge joint registration computes initial relative transform

- **WHEN** a `HingeJointDef` is processed during `Awake()` with obj1 at position (0,0,0), rotation identity and obj2 at position (1,0,0), rotation identity
- **THEN** `initial_rel_rotation` is identity and `initial_rel_pos_local` is (1,0,0)
- **AND** these values are passed to `RegisterHingeJoint()`

#### Scenario: Hinge axis is validated and normalized

- **WHEN** `Awake()` processes a `HingeJointDef` with `m_hinge_axis_obj1 = (0, 2, 0)`
- **THEN** the axis is normalized to (0, 1, 0) before passing to `RegisterHingeJoint()`

#### Scenario: Zero-length hinge axis is rejected

- **WHEN** `Awake()` processes a `HingeJointDef` with `m_hinge_axis_obj1` whose normalized length is below 1e-6
- **THEN** an `SDL_LogError` is emitted and the hinge joint is not registered

### Requirement: GpuHingeJoint stores initial relative transform

The `GpuHingeJoint` struct (std430 compatible, 80 bytes) SHALL contain:
- `uint32_t obj1_index`, `uint32_t obj2_index`, `float compliance`, `float _pad`
- `glm::vec4 hinge_axis_obj1` — hinge axis in obj1's local frame
- `glm::vec4 hinge_anchor_obj1` — hinge anchor in obj1's local frame
- `glm::vec4 initial_rel_pos_local` — `q1_init⁻¹ * (pos2_init - pos1_init)`
- `glm::vec4 initial_rel_rotation` — `q1_init⁻¹ * q2_init` as a quaternion (xyzw)

The struct SHALL NOT contain `obj2_local_aligned_axis`, `obj2_local_attach_point`, `obj1_local_aligned_axis`, or `obj1_local_attach_point` fields.

#### Scenario: GpuHingeJoint size remains 80 bytes

- **WHEN** `sizeof(GpuHingeJoint)` is evaluated in C++
- **THEN** the size is 80 bytes (5 × vec4 equivalent)

#### Scenario: Fields match between C++ and GLSL

- **WHEN** the C++ `GpuHingeJoint` struct and the GLSL `GpuHingeJoint` struct are compared
- **THEN** field names, types, and ordering match for std430 compatibility

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

- CPU: `GpuHingeJoint` fields, `HingeJointDef` fields, `RegisterHingeJoint` parameters
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

### Requirement: RegisterHingeJoint accepts initial relative transform

`PhysicsScene::RegisterHingeJoint()` SHALL accept parameters in the order:
1. `uint32_t obj1_index`, `uint32_t obj2_index`
2. `float compliance`
3. `const glm::vec3 &hinge_axis_obj1`
4. `const glm::vec3 &hinge_anchor_obj1`
5. `const glm::vec3 &initial_rel_pos_local` (position before rotation, consistent with `RegisterFixedJoint`)
6. `const glm::quat &initial_rel_rotation`

The function SHALL construct a `GpuHingeJoint` with all fields and append it to `m_hinge_joints`.

#### Scenario: RegisterHingeJoint packs GPU struct

- **WHEN** `RegisterHingeJoint(0, 1, 0.0, axis, anchor, rel_pos, rel_rot)` is called
- **THEN** a `GpuHingeJoint` is appended to `m_hinge_joints`
- **AND** all vec3 values are packed into vec4 fields with w = 0.0 (or w = quat.w for rotation)
