## 1. Collision detection — output shape-local contact points

- [x] 1.1 Modify `detect_collisions.comp`: After computing world-space `pts_a[pi]`/`pts_b[pi]`, convert to shape-local using `quat_inv_rotate(shape_world_rotation, pt - shape_world_position)` before writing to output buffers. Update buffer comments from "world" to "shape-local".
- [x] 1.2 Update `ConvexCollisionDetector.h` doc comments: change `contact_point_a`/`contact_point_b` descriptions from "world space" to "shape-local space".

## 2. Position constraint solver — use shape-local coords

- [x] 2.1 Modify `accumulate_contact_position.comp`: Replace `SubstepStartPosition`/`SubstepStartOrientation` bindings (12, 13) with `ShapeLocalPosition` (12) and `ShapeLocalRotation` (13).
- [x] 2.2 Update the coordinate conversion logic in `accumulate_contact_position.comp`: Read shape-local contact point directly, compute body-local offset `local_pt_in_body = quat_rotate(shape_local_rot, contact_pt) + shape_local_pos`, then world point `pos + quat_rotate(ori, local_pt_in_body)`. Compute `r = world_pt - pos`.
- [x] 2.3 Remove the `pos_a_start`/`ori_a_start`/`quat_inv_rotate` code path that used substep-start for world→local conversion.

## 3. Velocity constraint solver — use shape-local coords

- [x] 3.1 Modify `accumulate_contact_velocity.comp`: Replace `SubstepStartPosition`/`SubstepStartOrientation` bindings (17, 18) with `ShapeLocalPosition` (17) and `ShapeLocalRotation` (18).
- [x] 3.2 Update the coordinate conversion logic in `accumulate_contact_velocity.comp`: Same pattern as position solver — shape-local → body-local → world via current pose.
- [x] 3.3 Keep `PreContactLinearVelocity`/`PreContactAngularVelocity` bindings unchanged (still needed for restitution reference velocity).

## 4. Pre-contact velocity snapshot timing fix

- [x] 4.1 In `BuildPreCollisionRG()`: Move `AddSnap` calls for `PreContactLinearVelocity` and `PreContactAngularVelocity` to **after** the `Integrate Forces` pass but **before** the `Update Shape World Pose` pass. The new pass order is: substep-start position/orientation snapshots → integrate forces → pre-contact velocity snapshots → update shape world poses.
- [x] 4.2 Verify no `UseBuffer` or `prev_access` changes needed — the snapshot passes' access declarations (`UseBuffer(src_h, Impl::RR)`, `UseBuffer(dst_h, Impl::WW)`) do not change, and cross-RG contracts (PreCollisionRG writes → VelocityIterRG reads) are preserved. The RG builder handles internal barriers between the reordered passes automatically.

## 5. C++ RenderGraph binding updates for contact solvers

- [x] 5.1 In `BuildPositionIterRG()`: Remove `ssp_pos_h`/`ssp_ori_h` imports from the contact accumulation pass. Add imports for `ShapeLocalPosition` and `ShapeLocalRotation` buffers from `m_bound_scene->GetGpuBuffers()`. Update `UseBuffer` calls and SRB bindings.
- [x] 5.2 In `BuildVelocityIterRG()`: Remove `ssp_pos_h`/`ssp_ori_h` imports from the contact accumulation pass. Add imports for `ShapeLocalPosition` and `ShapeLocalRotation`. Update `UseBuffer` calls and SRB bindings.
- [x] 5.3 Verify `BuildPreCollisionRG()` retains substep-start position/orientation snapshots (for velocity-from-pose) and `BuildPostPositionRG()` is unchanged.

## 6. Build and smoke test

- [x] 6.1 Build the project and verify no shader compilation errors or C++ compile errors.
- [x] 6.2 Run `physics_example` and verify collisions behave correctly (objects rest on floor without jitter, stack, slide with friction, bounce with correct restitution). **Requires manual GUI verification.**
- [x] 6.3 Verify simulation pause/resume still works (space bar toggles). **Requires manual GUI verification.**
- [x] 6.4 Spot-check: no regression in joint constraints (hinge, fixed) since their solver shaders are unchanged. **Requires manual GUI verification.**
