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

## Physics Descriptors

- **GO-space Descriptor** — `RigidBodyDescriptor`, `CollisionShapeDescriptor`,
  `JointSubmitData`. Built by components during Init from component fields +
  GO world transforms. Submitted to Adaptor.
- **COM-space Descriptor** — `RigidBodyComDescriptor`,
  `CollisionShapeComDescriptor`, `GpuFixedJoint`, `GpuHingeJoint`. Built by
  Adaptor during Flush after COM computation and GO→COM conversion. Submitted
  to PhysicsScene for SoA storage and GPU upload.
