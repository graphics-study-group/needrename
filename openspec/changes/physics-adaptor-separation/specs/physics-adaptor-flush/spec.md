## ADDED Requirements

### Requirement: Flush resolves collision filters from pending descriptors
During `Flush`, the Adaptor SHALL resolve all `ObjectHandle` entries in pending shape descriptors' `ignore_collision_objects` to shape indices. For each ObjectHandle, the Adaptor SHALL:
1. Look up the `GameObject` via `Scene::GetGameObject()`
2. Find the directly-attached `CollisionShapeComponent` on that GameObject
3. Get its shape index via the Adaptor's `ComponentHandle → shape_idx` mapping

If the GameObject has no `CollisionShapeComponent` or the component is not yet registered, the handle SHALL be silently skipped.

#### Scenario: ObjectHandle resolved to shape index
- **WHEN** GameObject B has a registered `CollisionShapeComponent` with shape index 3
- **AND** shape 0's pending descriptor contains `ignore_collision_objects = [handle_B]`
- **THEN** after resolution, shape 0's resolved filter contains `[3]`

#### Scenario: Symmetric filter enforced
- **WHEN** shape 0 resolves to filter shape 3
- **THEN** shape 3's resolved filter also contains 0

### Requirement: Flush computes COM and inertia from pending descriptors
For each pending rigid body descriptor, the Adaptor SHALL compute center-of-mass position, COM offset, inertia tensor, and shape local poses using `ComInertiaComputer::Compute`. The computation SHALL use shape data (world positions, types, features) from pending shape descriptors belonging to that rigid body.

If `use_manual_inertia_com` is true, the computation SHALL use the manual inertia tensor and manual center-of-mass directly.

#### Scenario: Volume-weighted COM
- **WHEN** a rigid body has two attached box shapes of equal volume at world positions (0,0,0) and (2,0,0)
- **THEN** the computed COM is at (1,0,0)

#### Scenario: Manual inertia skips automatic computation
- **WHEN** `use_manual_inertia_com` is true with `manual_inertia = diag(1,2,3)` and `manual_center_of_mass = (0.5, 0, 0)`
- **THEN** the inertia tensor is set to the manual value and the COM offset is (0.5, 0, 0)

#### Scenario: Shape local poses recomputed relative to COM
- **WHEN** COM is computed at (1,0,0)
- **AND** a shape was at world position (2,0,0)
- **THEN** the shape's COM-local position is (1,0,0) = (2,0,0) - (1,0,0) in COM rotation frame

### Requirement: Flush converts joints from GO-local to COM-local
For each pending joint descriptor, the Adaptor SHALL convert the GO-local fields to COM-local using `JointConverter`. The conversion SHALL use COM offsets cached from the COM computation step.

Fixed joint conversion: `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`
Hinge joint conversion: `anchor_com = anchor_go - c1`; `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`

The conversion SHALL use COM offset vectors c1 (obj1) and c2 (obj2) in GO-local space:
- Fixed: `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`
- Hinge: `anchor_com = anchor_go - c1`; `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`
Hinge axis is a direction vector invariant under COM offset translation.

#### Scenario: Fixed joint converted
- **WHEN** a fixed joint has `go_rel_pos = (0, 0, 1)`, `go_rel_rot = identity`, `c1 = (0.2, 0, 0)`, `c2 = (0, 0, 0)`
- **THEN** the COM-local `rel_pos = (-0.2, 0, 1)`

#### Scenario: Hinge joint anchor converted
- **WHEN** a hinge joint has `anchor_go = (0, 0, 0.5)`, `c1 = (0, 0, 0.5)`
- **THEN** `anchor_com = (0, 0, 0)`

### Requirement: Flush submits COM descriptors to PhysicsScene
After computing COM, inertia, shape poses, and converting joints, the Adaptor SHALL build `RigidBodyComDescriptor` and `CollisionShapeComDescriptor` instances and submit them to PhysicsScene via `SubmitRigidBody` and `SubmitCollisionShape`. Converted joints SHALL be submitted via `SubmitFixedJoint` / `SubmitHingeJoint`.

#### Scenario: Full Flush cycle
- **WHEN** `Flush(render_system)` is called with pending rigid bodies, shapes, and joints
- **THEN** all COM descriptors are submitted to PhysicsScene
- **AND** `PhysicsScene::SyncGpuBuffers(render_system)` is called

### Requirement: Flush clears pending storage
After successful Flush, all pending maps (`m_pending_rigid_bodies`, `m_pending_shapes`, `m_pending_joints`) SHALL be cleared. The COM offset cache (`m_com_offsets`) SHALL persist for subsequent queries.

#### Scenario: Pending cleared after Flush
- **WHEN** `Flush` completes
- **THEN** all pending maps are empty

### Requirement: COM offset cache persists across Flush calls
`m_com_offsets[rb_idx]` SHALL be updated during each Flush for processed rigid bodies and SHALL remain available for `GetComOffsetLocal` queries between Flush calls.

#### Scenario: Offset available between Flush calls
- **WHEN** `Flush` has been called once
- **THEN** `GetComOffsetLocal(rb_idx)` returns valid data
- **AND** after a second `Flush`, the cache is refreshed with updated values
