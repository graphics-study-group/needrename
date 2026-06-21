## 1. Joint data structures (C++)

- [x] 1.1 Define `GpuFixedJoint` struct in `engine/Physics/PhysicsScene.h` — static input only: `obj1_index` (uint), `obj2_index` (uint), `compliance` (float), `_pad` (float), `initial_rel_pos_local` (vec4), `initial_rel_rotation` (vec4). Size = 48 bytes. No lagrange fields.
- [x] 1.2 Define `GpuHingeJoint` struct in `engine/Physics/PhysicsScene.h` — static input only: `obj1_index` (uint), `obj2_index` (uint), `compliance` (float), `_pad` (float), `obj1_local_aligned_axis` (vec4), `obj2_local_aligned_axis` (vec4), `obj1_local_attach_point` (vec4), `obj2_local_attach_point` (vec4). Size = 80 bytes. No lagrange fields.
- [x] 1.3 Define `FixedJointDef` and `HingeJointDef` structs (for use by `PhysicsConstraintComponent`) either in the component header or a shared header.
- [x] 1.4 Verify struct size and layout compatibility between C++ and GLSL (static_asserts on sizeof).

## 2. PhysicsScene: CPU vectors and GPU buffers (static input only)

- [x] 2.1 Add `std::vector<GpuFixedJoint> m_fixed_joints` and `std::vector<GpuHingeJoint> m_hinge_joints` to PhysicsScene.
- [x] 2.2 Add `std::unique_ptr<ComputeBuffer>` members `m_gpu_fixed_joints` and `m_gpu_hinge_joints` to PhysicsScene.
- [x] 2.3 Add `gpu_fixed_joints`, `gpu_hinge_joints`, `fixed_joint_count`, `hinge_joint_count` to `PhysicsGpuBuffers` struct.
- [x] 2.4 Implement `RegisterFixedJoint()` — append to `m_fixed_joints` (static data only, no lagrange).
- [x] 2.5 Implement `RegisterHingeJoint()` — append to `m_hinge_joints` (static data only, no lagrange).
- [x] 2.6 Update `RefreshGpuBuffers()` to create/upload joint GPU buffers via `EnqueueBufferSubmission()` (read-only during solve).
- [x] 2.7 Update `Clear()` to empty joint vectors and reset joint GPU buffers.
- [x] 2.8 Update `DebugPrint()` to log joint counts.

## 3. GLSL compute shaders

- [x] 3.1 Create `engine/Physics/shader/solver/XPBDSolver/common/xpbd_joint_math.glsl` with shared helpers: `quat_inverse()`, quaternion-to-rotation-vector conversion for fixed joint.
- [x] 3.2 Create `engine/Physics/shader/solver/XPBDSolver/clear_hinge_lagrange.comp` — zeros solver-owned SoA buffers `HingeAlignedAxisLagrange` and `HingePositionLagrange` (one float per constraint each).
- [x] 3.3 Create `engine/Physics/shader/solver/XPBDSolver/clear_fixed_lagrange.comp` — zeros solver-owned SoA buffers `FixedRotationLagrange` and `FixedPositionLagrange` (one float per constraint each).
- [x] 3.4 Create `engine/Physics/shader/solver/XPBDSolver/accumulate_hinge_position.comp` — reads static joint data from PhysicsScene AoS buffer (read-only), reads lagrange from solver SoA buffers, implements aligned-axis + attachment-point constraints using Jacobi atomic accumulation into shared body delta/count buffers, writes lagrange back to SoA buffers.
- [x] 3.5 Create `engine/Physics/shader/solver/XPBDSolver/accumulate_fixed_position.comp` — reads static joint data from PhysicsScene AoS buffer (read-only), reads lagrange from solver SoA buffers, implements rotation constraint (drive toward initial relative rotation) + position constraint (drive toward initial relative transform), writes lagrange back to SoA buffers.
- [x] 3.6 Verify shaders compile cleanly through CMake pipeline (no GLSL errors).

## 4. XPBDGpuSolver: lagrange buffers, shader loading, and dispatch

- [x] 4.1 Add `ComputeStage` members to `XPBDGpuSolver::Impl` for the 4 new joint shaders (2 clear + 2 accumulate).
- [x] 4.2 Add SPIR-V vector members and load them in `EnsureInitialized()`.
- [x] 4.3 Add solver-owned SoA intermediate buffers to `Impl`:
  - `gpu_hinge_aligned_axis_lagrange` (float per hinge constraint)
  - `gpu_hinge_position_lagrange` (float per hinge constraint)
  - `gpu_fixed_rotation_lagrange` (float per fixed constraint)
  - `gpu_fixed_position_lagrange` (float per fixed constraint)
  Lazily allocate in `EnsureIntermediateBuffers()`, recreate when joint counts change.
- [x] 4.4 Update `EnsureIntermediateBuffers()` to accept joint counts alongside body_count and max_contacts.
- [x] 4.5 Add import handles for PhysicsScene joint AoS buffers and solver SoA lagrange buffers in `Step()`. Guard against nullptr when joint counts are zero.
- [x] 4.6 Insert lagrange-reset passes before the position iteration loop — dispatch clear_hinge_lagrange and clear_fixed_lagrange (skip if count is 0).
- [x] 4.7 Insert hinge accumulate + fixed accumulate passes inside the position iteration loop, after contact accumulate and before apply body deltas. Pass both the PhysicsScene joint AoS buffer and solver SoA lagrange buffers to each accumulate pass.
- [x] 4.8 Each joint accumulate pass skips dispatch when joint count is 0.

## 5. PhysicsConstraintComponent

- [x] 5.1 Create `engine/Framework/component/physics/PhysicsConstraintComponent.h` with `Component` base class, `REFL_SER_CLASS` macro, and storage for joint definitions (separate vectors or a variant collection for FixedJointDef / HingeJointDef).
- [x] 5.2 Create `engine/Framework/component/physics/PhysicsConstraintComponent.cpp` with `Awake()` implementation: validate own RigidBodyComponent, validate each joint's obj2 has RigidBodyComponent, compute FixedJoint initial relative transform (`q1⁻¹ * q2` and `q1⁻¹ * (pos2 - pos1)`), call PhysicsScene registration methods.
- [x] 5.3 Add reflection registration: ensure the generated `__generated__/meta_engine/` file is created or updated for the new component type.
- [x] 5.4 Add `PhysicsConstraintComponent` include to any component registry or factory if one exists.

## 6. SceneBuilder and double pendulum demo

- [x] 6.1 Add `AddDoublePendulum(glm::vec3 anchor_position, float spacing)` method declaration to `SceneBuilder`.
- [x] 6.2 Implement `AddDoublePendulum()`: create kinematic sphere (radius=0.3), 2 dynamic boxes (elongated, mass=1.0), 1 dynamic cylinder (oriented horizontally, mass=0.5); add HingeJoints linking sphere→box1→box2; add FixedJoint linking box2→cylinder.
- [x] 6.3 Wire pendulum into `example/physics_example/main.cpp` — call `builder.AddDoublePendulum()` with reasonable parameters (e.g., anchor at (0,0,4), spacing=0.15).
- [x] 6.4 Ensure the cylinder uses a distinct material and horizontal orientation (rotated 90° around Y) so rotation is visually obvious when the pendulum swings.

## 7. Build, test, and verify

- [x] 7.1 Build the engine and example — resolve any compilation errors.
- [x] 7.2 Run the physics example — verify pendulum swings, joints hold, cylinder rotates with the bottom box, no bodies separate at joint points.
- [x] 7.3 Verify `DebugPrint()` shows correct joint counts.
- [x] 7.4 Verify no joint validation errors in log output.
- [x] 7.5 Test with simulation toggled off/on (SPACE key) — joints should persist and resume correctly after toggle.
