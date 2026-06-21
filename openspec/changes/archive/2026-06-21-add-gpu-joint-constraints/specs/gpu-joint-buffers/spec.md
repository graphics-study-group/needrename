# gpu-joint-buffers

## Purpose

Define how PhysicsScene manages CPU-side joint storage and GPU-side joint definition buffers. The packed AoS struct layouts contain only static input data — no runtime state. Lagrange multipliers are managed separately by the XPBD solver (see `gpu-joint-constraint-solving`).

## Requirements

### Requirement: PhysicsScene CPU-side joint vectors

PhysicsScene SHALL maintain separate CPU-side vectors for each joint type:
- `std::vector<GpuFixedJoint> m_fixed_joints`
- `std::vector<GpuHingeJoint> m_hinge_joints`

Each joint SHALL store resolved `uint32_t` rigid body indices (obj1, obj2) rather than engine handles.

#### Scenario: Joint vectors are cleared on Clear()

- **WHEN** `PhysicsScene::Clear()` is called
- **THEN** `m_fixed_joints` and `m_hinge_joints` are both emptied

### Requirement: GpuFixedJoint packed struct layout (static input only)

The `GpuFixedJoint` struct SHALL contain only static input data (no lagrange multipliers). Layout (std430 compatible):

- `uint32_t obj1_index` — rigid body index of the owning object
- `uint32_t obj2_index` — rigid body index of the second object
- `float compliance` — joint compliance
- `float _pad` — alignment padding
- `glm::vec4 initial_rel_pos_local` — `q1_init⁻¹ * (pos2_init - pos1_init)` in obj1's local frame
- `glm::vec4 initial_rel_rotation` — `q1_init⁻¹ * q2_init` as a quaternion (xyzw)

Total size: 48 bytes (3 × vec4 equivalent). No runtime state fields.

#### Scenario: GpuFixedJoint is sizeof-compatible with GLSL

- **WHEN** evaluated in C++ with `sizeof(GpuFixedJoint)`
- **THEN** the size matches the GLSL `GpuFixedJoint` struct in `accumulate_fixed_position.comp`

### Requirement: GpuHingeJoint packed struct layout (static input only)

The `GpuHingeJoint` struct SHALL contain only static input data (no lagrange multipliers). Layout (std430 compatible):

- `uint32_t obj1_index` — rigid body index of the owning object
- `uint32_t obj2_index` — rigid body index of the second object
- `float compliance` — joint compliance
- `float _pad` — alignment padding
- `glm::vec4 obj1_local_aligned_axis` — aligned axis in obj1's local frame
- `glm::vec4 obj2_local_aligned_axis` — aligned axis in obj2's local frame
- `glm::vec4 obj1_local_attach_point` — attachment point in obj1's local frame
- `glm::vec4 obj2_local_attach_point` — attachment point in obj2's local frame

Total size: 80 bytes (5 × vec4 equivalent). No angle limit, target angle, or lagrange fields.

#### Scenario: GpuHingeJoint is sizeof-compatible with GLSL

- **WHEN** evaluated in C++ with `sizeof(GpuHingeJoint)`
- **THEN** the size matches the GLSL `GpuHingeJoint` struct in `accumulate_hinge_position.comp`

### Requirement: GPU joint buffers created and refreshed

PhysicsScene SHALL create and manage `std::unique_ptr<ComputeBuffer>` GPU buffers for each joint type's static definition data. On `RefreshGpuBuffers()`, the CPU-side joint vectors SHALL be uploaded via `EnqueueBufferSubmission()`.

These buffers are read-only during the solve phase. Lagrange multiplier state is owned and managed by `XPBDGpuSolver::Impl` as separate SoA buffers.

#### Scenario: Joint buffers are exposed via PhysicsGpuBuffers

- **WHEN** `PhysicsScene::GetGpuBuffers()` is called
- **THEN** the returned struct includes `gpu_fixed_joints` and `gpu_hinge_joints` const pointers
- **AND** includes `fixed_joint_count` and `hinge_joint_count` as uint32_t values

#### Scenario: Joint buffers recreated on size change

- **WHEN** joint count changes between frames
- **THEN** `RefreshGpuBuffers()` recreates the GPU buffer with the new size
- **AND** uploads all joint definition data

### Requirement: Joint registration methods

PhysicsScene SHALL provide:

```cpp
void RegisterFixedJoint(uint32_t obj1_index, uint32_t obj2_index,
                        float compliance,
                        const glm::vec3 &initial_rel_pos_local,
                        const glm::quat &initial_rel_rotation);

void RegisterHingeJoint(uint32_t obj1_index, uint32_t obj2_index,
                        float compliance,
                        const glm::vec3 &obj1_local_aligned_axis,
                        const glm::vec3 &obj2_local_aligned_axis,
                        const glm::vec3 &obj1_local_attach_point,
                        const glm::vec3 &obj2_local_attach_point);
```

Both methods SHALL append a new joint entry to the respective CPU vector and mark GPU buffers as needing refresh. No lagrange initialization is needed since runtime state is managed by the solver.

#### Scenario: RegisterFixedJoint appends to vector

- **WHEN** `RegisterFixedJoint` is called with valid indices
- **THEN** a new `GpuFixedJoint` entry is appended to `m_fixed_joints`
- **AND** the entry contains only static definition data (indices, compliance, initial transform)

#### Scenario: RegisterHingeJoint appends to vector

- **WHEN** `RegisterHingeJoint` is called with valid indices
- **THEN** a new `GpuHingeJoint` entry is appended to `m_hinge_joints`
- **AND** the entry contains only static definition data (indices, compliance, axes, attach points)
