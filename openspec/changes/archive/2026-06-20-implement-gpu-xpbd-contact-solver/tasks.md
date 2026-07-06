## 1. Collision Detector API Refactor

- [x] 1.1 Define `CollisionResultBuffers` POD struct in `ConvexCollisionDetector.h` with `const ComputeBuffer*` fields for all result buffers plus `max_collision_pairs`
- [x] 1.2 Add `GetCollisionResultBuffers()` method, remove the 6 individual `Get*()` getter methods
- [x] 1.3 Update any callers of the removed getters

## 2. Shared GLSL Math Utilities

- [x] 2.1 Create `engine/Physics/shader/solver/XPBDSolver/common/xpbd_math.glsl` with quat_rotate, quat_inv_rotate, apply_world_inv_inertia, quat_normalize, multiply_mat4_3x3_vec3 (vec3_dot/vec3_cross removed in cleanup — built-in GLSL dot/cross used instead)
- [x] 2.2 Add `quat_mul(vec4 a, vec4 b)` function for quaternion multiplication (needed by update_shape_world_pose.comp)

## 3. Snapshot & Clear Shaders

- [x] 3.1 Create `snapshot_position.comp`: copies one vec4[] buffer to another, one thread per body
- [x] 3.2 Create `clear_int_buffer.comp`: zeros an int[] buffer
- [x] 3.3 SPIR-V compilation handled automatically by existing CMake glob

## 4. Force Integration Shader

- [x] 4.1 Create `integrate_forces.comp`: semi-implicit Euler with gravity, external forces, torques, and Coriolis term in body frame; gravity read from `XpbdUniforms` SSBO
- [x] 4.2 Bind required PhysicsScene buffers plus `gpu_uniforms` for gravity/dt

## 5. Shape World Pose Update Shader (NEW)

- [x] 5.1 Create `update_shape_world_pose.comp`: per-shape, recompute `world_pos = body_pos + quat_rotate(body_ori, local_pos)` and `world_ori = quat_mul(body_ori, local_ori)`; skip dead shapes; unbound shapes copy local→world
- [x] 5.2 Add `ComputeStage` + SPIR-V loading for the new shader in `XPBDGpuSolver::Impl`
- [x] 5.3 Add the shape world update pass to the substep loop (after integrate, before collision detection)
- [x] 5.4 Import `shape_world_position`, `shape_world_rotation`, `shape_local_position`, `shape_local_rotation`, `shape_alive` PhysicsScene buffers in `Step()`

## 6. Position Constraint Solve Shaders

- [x] 6.1 Create `accumulate_contact_position.comp`: per-contact XPBD — read collision results, map shape→body, compute local→world contact points from substep-start pose, atomically accumulate position deltas, update lagrange
- [x] 6.2 Create `apply_body_position_deltas.comp`: per-body — average accumulated deltas, apply position + quaternion update, reset accumulators
- [x] 6.3 Bind collision result buffers, shape→body mapping, substep-start snapshots, PhysicsScene body buffers

## 7. Velocity Update Shader

- [x] 7.1 Create `update_velocities_from_pose.comp`: compute `v = (p_post - p_pre) / dt` and `w = 2 * sign(dq.w) * dq.xyz / dt` from pre-gravity snapshots
- [x] 7.2 Bind pre-gravity snapshot buffers and current position/orientation/velocity buffers

## 8. Velocity Constraint Solve Shaders

- [x] 8.1 Create `accumulate_contact_velocity.comp`: per-contact — relative velocity at contact point, restitution + friction using accumulated lagrange, atomically accumulate velocity deltas
- [x] 8.2 Create `apply_body_velocity_deltas.comp`: per-body — average accumulated velocity deltas, apply, reset
- [x] 8.3 Bind pre-contact velocity snapshot buffers for restitution reference velocity

## 9. XPBDGpuSolver C++ Orchestration

- [x] 9.1 Add intermediate GPU buffer members to `XPBDGpuSolver::Impl` (snapshot buffers, delta accumulators, lagrange buffer, uniforms, count buffers)
- [x] 9.2 Implement `EnsureIntermediateBuffers()` sized by max body count and max contacts
- [x] 9.3 Add `XpbdConfig` parameter with `gravity`, `num_substep_perstep`, `num_iter_persubstep`, `num_velocity_iters`
- [x] 9.4 Implement the substep+iteration render graph pass dispatch loop in `Step()`
- [x] 9.5 Load all SPIR-V files and create `ComputeStage` + `ComputeResourceBinding` for each shader
- [x] 9.6 Bind correct buffers to each shader resource binding per pass
- [x] 9.7 Add simulation toggle check (`IsSimulationEnabled()`) in each solver pass lambda, evaluated at dispatch time
- [x] 9.8 Model matrix update runs at the END of Step() (after all substeps), always executes regardless of simulation toggle

## 10. Move Collision Detector into Solver (NEW)

- [x] 10.1 Add `std::unique_ptr<ConvexCollisionDetector> collision_detector` to `XPBDGpuSolver::Impl`
- [x] 10.2 Add `EnsureCollisionDetector(uint32_t shape_count)` method — creates/recreates detector when shape count changes
- [x] 10.3 Remove `const CollisionResultBuffers &collision_results` from `Step()` signature in both .h and .cpp
- [x] 10.4 Import collision result buffers internally from owned detector in `Step()`

## 11. Per-Substep Collision Detection (NEW)

- [x] 11.1 Move collision detection (`collision_detector->Step()`) into the substep loop, after shape world update and before lagrange memset
- [x] 11.2 Remove collision detection from `PhysicsExampleRenderGraphBuilder::BuildRenderGraph()`
- [x] 11.3 Remove `m_collision_detector` member and `#include` of `ConvexCollisionDetector.h` from example

## 12. Shadow Map & Example Fixes (NEW)

- [x] 12.1 Add `.UseBuffer(mm_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})` to each Shadowmap Pass in `PhysicsExampleRenderGraphBuilder.cpp`
- [x] 12.2 Update `m_xpbd_solver->Step()` call to new signature: `Step(rgb, physics_scene, mm_handle)`

## 13. Remove Placeholder Shader

- [x] 13.1 Empty the old `step.comp` placeholder (no-op shader)
- [x] 13.2 Remove `step.comp.spv` loading code from XPBDGpuSolver (never loaded in new code)

## 14. Verification

- [x] 14.1 Build the engine with all new shaders — verify SPIR-V compilation is clean
- [x] 14.2 Run physics test scene — verify bodies fall under gravity, collide with ground, and settle
- [x] 14.3 Stack test — verify a stack of boxes remains stable for several seconds
- [x] 14.4 Verify model matrix rendering (objects visible and correctly positioned)
- [x] 14.5 Verify shadow maps render at correct positions (model matrix dependency respected)
- [x] 14.6 Verify simulation toggle (SPACE) pauses and resumes correctly
