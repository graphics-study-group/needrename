## 1. RigidBodyComponent — COM offset and inertia rotation

- [x] 1.1 In the inertial attachment loop (step 3), add `rb.m_manual_center_of_mass = UrdfToEnginePos(inertial.origin_xyz)` so the COM offset is populated from URDF `<inertial>/<origin>`
- [x] 1.2 Build the 3x3 inertia matrix I from URDF `ixx, ixy, ixz, iyy, iyz, izz` values using the existing `glm::mat3` construction pattern
- [x] 1.3 If `inertial.origin_rpy` is non-zero, compute `R = glm::mat3_cast(UrdfRpyToEngineQuat(inertial.origin_rpy))` and rotate the inertia tensor: `I_link = transpose(R) * I * R`
- [x] 1.4 Extract `m_manual_inertia_diag` and `m_manual_inertia_offdiag` from the (possibly rotated) inertia tensor
- [x] 1.5 Use `m_use_manual_inertia_com` (renamed field from `fix-center-of-mass-offset`) instead of the old `m_use_manual_inertia`

## 2. CollisionShapeComponent — always child GOs

- [x] 2.1 Replace the conditional sub-GO logic (step 4) with unconditional child GO creation: one child GO per `<collision>` element
- [x] 2.2 Name each collision child GO `"{link_name}_collision_{i}"` where `i` is the 0-based index
- [x] 2.3 Set the child GO's local `Transform` from `col.origin_xyz` / `col.origin_rpy` (converted to engine coords), with scale `(1, 1, 1)`
- [x] 2.4 Attach `CollisionShapeComponent` to the child GO with `m_shape_type` and `m_feature` set as before, and `m_center = (0, 0, 0)`, `m_rotation = identity`
- [x] 2.5 Skip collision elements with `UrdfGeometryType::Mesh` (log info, Phase 2)

## 3. PhysicsConstraintComponent — on child GO, RB check

- [x] 3.1 Before creating constraints for a joint, check that both parent and child links have `<inertial>` (both have `Go` entries with `RigidBodyComponent`). Skip the joint if either side lacks inertial
- [x] 3.2 Instead of the `parent_constraints` map, create `PhysicsConstraintComponent` on the **child** link GO
- [x] 3.3 For `FixedJointDef`: set `m_obj2_handle = parent_go->GetHandle()`, `m_compliance = 0.0f`
- [x] 3.4 For `HingeJointDef`: set `m_obj2_handle = parent_go->GetHandle()`, `m_hinge_anchor_obj1 = (0, 0, 0)`, `m_hinge_axis_obj1 = UrdfAxisToEngine(joint.axis)` (no rpy rotation needed), `m_compliance = 0.0f`
- [x] 3.5 Remove the `parent_constraints` map and associated aggregation logic

## 4. StaticMeshComponent — separate visual child GOs

- [x] 4.1 For each `<collision>` element with primitive geometry, create a **visual** child GO named `"{link_name}_visual_{i}"` (separate from the collision child GO from section 2)
- [x] 4.2 Set the visual child GO's local `Transform` from `col.origin_xyz` / `col.origin_rpy` (converted), with scale set to the appropriate mesh scale (box half-extents, sphere radius, cylinder (r, r, half_h))
- [x] 4.3 Attach `StaticMeshComponent` to the visual child GO with `m_mesh_asset` referencing the builtin mesh, `m_material_assets` using hash-determined material, `m_is_eagerly_loaded = true`
- [x] 4.4 Skip mesh-type collision elements (DAE/STL) for visual as well (log info)

## 5. Collision filtering — verify unchanged

- [x] 5.1 Verify `CollectCollisionObjectHandles` still correctly finds collision child GOs in the new hierarchy (subtree traversal is depth-independent)
- [x] 5.2 Verify `add_ignores` propagates ignore lists through the new child GO structure
- [x] 5.3 No code changes expected — verify by inspection that subtree traversal works with collision shapes at depth 1 instead of depth 0

## 6. Build and verify

- [x] 6.1 Compile the project (`cmake --build build`) and fix any compilation errors
- [ ] 6.2 Test import of `a1.urdf` — verify `GO_a1.asset` is produced and has correct GameObject count
- [ ] 6.3 Verify link GO hierarchy matches URDF joint tree (parent-child relationships correct)
- [ ] 6.4 Verify collision child GOs exist for each collision element with correct Transforms
- [ ] 6.5 Verify visual child GOs exist with correct mesh scales
- [ ] 6.6 Verify `PhysicsConstraintComponent` is on the correct (child) GO with `m_hinge_anchor_obj1 = (0,0,0)`
- [ ] 6.7 Load `GO_a1.asset` into a physics scene and verify no constraint resolution errors in Init
