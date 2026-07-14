# Domain Glossary

## Coordinate Spaces

- **GO-local space** — Offsets measured relative to a GameObject's transform
  (position + rotation). All component-level editable fields use this space.
  Example: `HingeJointDef::m_hinge_anchor_obj1` is the anchor offset from obj1's
  GO origin, rotated into obj1's GO orientation.
- **GO world space** — The result of applying the GO transform to GO-local
  coordinates. `GameObject::GetWorldTransform()` returns this.
- **COM space** — Offsets measured relative to the center-of-mass of a rigid
  body. The COM world pose is `center_world_position` + `center_world_rotation`.
  All GPU physics buffers use this space.
- **COM-local space** — Offsets from the COM origin, rotated into the COM
  orientation. Derived from GO-local by subtracting the COM offset.
  `center_offset_local_position` is the GO→COM vector expressed in GO-local.

## Conversion

- **GO→COM conversion** — Subtracting the COM offset (and applying rotation
  when present) to transform GO-local coordinates into COM-local.
  Performed by `PhysicsAdaptor` during `FlushPhysics`, before data reaches
  `PhysicsScene`. GPU shaders never perform this conversion — they operate
  entirely in COM space.

## Architecture Layers

- **PhysicsAdaptor** — The sole bridge between GO space and COM space. Owned by
  Scene, ran after all Init events. Receives GO-space Descriptors from
  components, computes COM/inertia, converts coordinates, resolves collision
  filters, and submits COM-space Descriptors to PhysicsScene. Components never
  see PhysicsScene directly. Located in `Framework/world/physics/`.
- **PhysicsScene** — Pure COM-space physics storage. Manages SoA arrays, slot
  allocation, RB↔shape topology, and GPU buffer synchronization. Has no
  knowledge of GameObject, Component, or ObjectHandle. Located in `Physics/`.
  _Constraint_: `Physics/` must not include any Framework headers.

## Physics Data Flow

- **Component field** → GO-local value set by user
- **Awake** → `Adaptor::AllocateSlot(handle)` — reserves a slot index, no
  property values submitted
- **Init** → Component builds GO-space Descriptor from its fields + GO world
  transform, calls `Adaptor::Submit*(idx, desc)`
- **Scene::FlushPhysics** → Adaptor processes all pending:
  - Resolves collision filters (ObjectHandle → shape indices)
  - Computes COM and inertia tensor (volume-weighted, parallel axis theorem)
  - Converts joints from GO-local to COM-local (pure function)
  - Builds COM-space Descriptors and submits to PhysicsScene
- **GPU buffer** → COM-local values uploaded by `PhysicsScene::SyncGpuBuffers`,
  consumed by solvers

## URDF Import Hierarchy

- **Link GO** — A GameObject representing one URDF `<link>`. Positioned at the
  URDF link frame (set by the incoming joint's `<origin>`, or world origin for
  the root link). Carries `RigidBodyComponent` when the link has `<inertial>`.
  `m_manual_center_of_mass` stores the COM offset from `<inertial>/<origin>` in
  GO-local space. Inertia tensor is rotated from the inertial frame to the link
  frame when `<inertial>/<origin>` has non-zero `rpy`: `I_link = Rᵀ * I * R`.
- **Collision child GO** — A direct child of the link GO, one per `<collision>`
  element. Carries `CollisionShapeComponent` with `m_center = 0,
  m_rotation = 0` — the collision offset is encoded in the child GO's
  `Transform`. Scale is always identity.
- **Visual child GO** — A direct child of the link GO, one per `<collision>`
  element (Phase 1: visual geometry data is sourced from `<collision>` until
  DAE/STL mesh import is implemented). Carries `StaticMeshComponent`. Mesh
  scale is encoded in the child GO's `Transform` scale component.
- **Constraint on child rule** — `PhysicsConstraintComponent` for a URDF joint
  is placed on the **child** link GO (not parent). `HingeJointDef::m_obj2_handle`
  references the parent link GO. This eliminates the need to transform the
  joint axis through `joint.origin.rpy` — the axis is already in the child link
  frame.

## Physics Descriptors

- **GO-space Descriptor** — `RigidBodyDescriptor`, `CollisionShapeDescriptor`,
  `JointSubmitData`. Built by components during Init from component fields +
  GO world transforms. Submitted to Adaptor.
- **COM-space Descriptor** — `RigidBodyComDescriptor`,
  `CollisionShapeComDescriptor`, `GpuFixedJoint`, `GpuHingeJoint`. Built by
  Adaptor during Flush after COM computation and GO→COM conversion. Submitted
  to PhysicsScene for SoA storage and GPU upload.
