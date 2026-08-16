## ADDED Requirements

### Requirement: PhysicsAdaptor owned by Scene
`Scene` SHALL own a `PhysicsAdaptor` instance, accessible via `GetPhysicsAdaptor()`. The Adaptor SHALL be lazy-created on first access. Only one Adaptor SHALL exist per Scene.

#### Scenario: Adaptor accessible from Scene
- **WHEN** `scene.GetPhysicsAdaptor()` is called
- **THEN** a valid `PhysicsAdaptor&` is returned
- **AND** subsequent calls return the same instance

#### Scenario: Components access Adaptor through Scene
- **WHEN** a `RigidBodyComponent` calls `GetScene()->GetPhysicsAdaptor()`
- **THEN** the Adaptor is accessible without directly holding a reference

### Requirement: Rigid body slot allocation
`PhysicsAdaptor::AllocateSlot(ObjectHandle owner)` SHALL allocate a rigid body slot in the underlying `PhysicsScene`, returning the slot index. If the handle already has an allocated slot, the existing index SHALL be returned. Allocation SHALL be idempotent across multiple calls with the same handle.

#### Scenario: First allocation returns new index
- **WHEN** `AllocateSlot(handle_A)` is called for handle A for the first time
- **THEN** a new index is returned and the handle→index mapping is established

#### Scenario: Repeated allocation returns same index
- **WHEN** `AllocateSlot(handle_A)` is called twice with the same handle
- **THEN** both calls return the same index

### Requirement: Collision shape slot allocation
`PhysicsAdaptor::AllocateShapeSlot(ComponentHandle owner)` SHALL allocate a shape slot. Idempotent — repeated calls with the same ComponentHandle return the same index.

#### Scenario: Shape allocation
- **WHEN** `AllocateShapeSlot(comp_handle)` is called
- **THEN** a valid shape index is returned and the component→index mapping is established

### Requirement: Joint slot allocation
`PhysicsAdaptor::AllocateFixedJoint()` and `AllocateHingeJoint()` SHALL allocate fixed and hinge joint slots respectively. Each call returns a new index. Joint slots are not idempotent (each call creates a new slot).

#### Scenario: Multiple joint allocations
- **WHEN** `AllocateFixedJoint()` is called 3 times
- **THEN** three distinct indices are returned (e.g., 0, 1, 2)

### Requirement: Adaptor maintains handle mappings
The Adaptor SHALL maintain these mappings internally:
- `ObjectHandle → rb_idx` (forward lookup, used by `FindRigidBodyByObjectHandle`)
- `ComponentHandle → shape_idx` (shape component lookup)
- `rb_idx → ObjectHandle` (reverse lookup, used by collision filter resolution)
- `rb_idx → std::vector<uint32_t>` (shape indices per rigid body)

#### Scenario: Forward lookup
- **WHEN** `FindRigidBodyByObjectHandle(handle)` is called with a previously-allocated handle
- **THEN** the correct rigid body index is returned

#### Scenario: Reverse lookup for filter resolution
- **WHEN** Adaptor needs to find which GameObject owns a given rigid body index
- **THEN** the rb_idx→ObjectHandle mapping provides the answer

### Requirement: Adaptor exposes COM offset query
`PhysicsAdaptor::GetComOffsetLocal(uint32_t rb_idx)` SHALL return the GO→COM offset vector in GO-local space. The offset SHALL be computed during `Flush` and persist for subsequent queries. If the index is invalid, SHALL return zero vector.

#### Scenario: COM offset available after Flush
- **WHEN** `Flush` has completed for a rigid body
- **THEN** `GetComOffsetLocal(rb_idx)` returns the non-zero COM offset computed during Flush

#### Scenario: COM offset zero before Flush
- **WHEN** a rigid body has been allocated but Flush has not yet run
- **THEN** `GetComOffsetLocal(rb_idx)` returns `(0, 0, 0)`

### Requirement: Adaptor physics activation switch
`PhysicsAdaptor` SHALL expose `SetPhysicsActive(bool)` and `IsPhysicsActive()`. When active, RendererComponents SHALL use the physics-driven model matrix path. When inactive, RendererComponents SHALL use their GO world transform directly. The switch SHALL be independent of `PhysicsScene::SetSimulationEnabled`.

#### Scenario: Physics active enables COM-following rendering
- **WHEN** `IsPhysicsActive()` returns true
- **THEN** RendererComponent queries Adaptor for COM offset and submits offset matrix for composition

#### Scenario: Physics inactive uses GO world transform
- **WHEN** `IsPhysicsActive()` returns false
- **THEN** RendererComponent submits `model_mat_index = -1` and uses its GO world transform directly

### Requirement: Adaptor RB→Shape binding
`PhysicsAdaptor::BindShapeToRigidBody(uint32_t shape_idx, uint32_t rb_idx)` SHALL establish a binding between a shape and its owning rigid body. The binding SHALL be reflected in the shape COM descriptor (`bound_rigid_body` field) during Flush. Calling with `INVALID_INDEX` as rb_idx SHALL unbind the shape.

#### Scenario: Shape bound to rigid body
- **WHEN** `BindShapeToRigidBody(2, 0)` is called
- **THEN** shape index 2 is associated with rigid body index 0

#### Scenario: Shape unbound
- **WHEN** `BindShapeToRigidBody(2, INVALID_INDEX)` is called
- **THEN** shape index 2 no longer has a rigid body binding

### Requirement: Adaptor submit methods store GO descriptors
`SubmitRigidBody(uint32_t idx, const RigidBodyDescriptor&)`, `SubmitShape(uint32_t idx, const CollisionShapeDescriptor&)`, and `SubmitJoint(uint32_t idx, const JointSubmitData&)` SHALL store the submitted descriptor in internal pending maps keyed by index. Repeated submissions with the same index SHALL overwrite the previous descriptor.

#### Scenario: Submit overwrites previous
- **WHEN** `SubmitRigidBody(0, desc_A)` is called followed by `SubmitRigidBody(0, desc_B)`
- **THEN** the pending map contains `desc_B` for index 0
