# hinge-joint-constraint

## MODIFIED Requirements

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
