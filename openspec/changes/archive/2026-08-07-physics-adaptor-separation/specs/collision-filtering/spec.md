## MODIFIED Requirements

### Requirement: PhysicsScene stores unresolved filter ObjectHandles
~~`PhysicsScene` SHALL store the raw `std::vector<ObjectHandle>` per shape upon registration. During `RegisterCollisionShape`, the ignore list from the component is stored in a pending filter handles array keyed by shape index.~~

`PhysicsAdaptor` SHALL store the raw `std::vector<ObjectHandle>` per shape in the pending `CollisionShapeDescriptor`. The ignore list from the component is carried into the descriptor during `CollisionShapeComponent::Init`. Resolution happens during `Adaptor::Flush`, not during `RegisterCollisionShape`.

#### Scenario: Filter handles stored at registration
- **WHEN** ~~`RegisterCollisionShape` is called for a component with `m_ignore_collision_objects = [handle_X]`~~
- **WHEN** `SubmitShape(idx, desc)` is called with `desc.ignore_collision_objects = [handle_X]`
- **THEN** the ObjectHandle `[handle_X]` is stored in the Adaptor's pending descriptor for that shape index

### Requirement: Deferred filter resolution
~~`PhysicsScene` SHALL provide a `ResolveCollisionFilters(Scene &scene)` method that resolves all pending filter ObjectHandles to shape indices.~~

`PhysicsAdaptor` SHALL resolve all pending filter ObjectHandles to shape indices during `Flush`. Resolution follows the same algorithm: for each unresolved ObjectHandle, look up the GameObject, find the CollisionShapeComponent, get its shape index. Resolution SHALL handle multiple CollisionShapeComponents on a single GameObject as a safety measure.

The resolution SHALL be called as part of `Flush`, after all Init events and before COM descriptors are submitted to PhysicsScene. After resolution, pending ObjectHandles SHALL be cleared.

#### Scenario: ObjectHandle resolved to shape index
- **WHEN** GameObject B has a `CollisionShapeComponent` with shape index 3
- **AND** shape 0's pending descriptor contains `ObjectHandle_B`
- **THEN** after resolution, shape 0's resolved filter contains `[3]`

#### Scenario: ObjectHandle with no CollisionShapeComponent produces empty filter
- **WHEN** GameObject B has no `CollisionShapeComponent`
- **AND** shape 0's pending descriptor contains `ObjectHandle_B`
- **THEN** after resolution, shape 0's resolved filter is empty (the handle is silently ignored)

#### Scenario: Unregistered shape handle skipped
- **WHEN** GameObject B's `CollisionShapeComponent` is not yet registered
- **AND** shape 0's pending descriptor contains `ObjectHandle_B`
- **THEN** that handle is skipped during resolution

### Requirement: GPU filter data buffers
~~`PhysicsScene` SHALL own GPU buffers for collision filter data.~~

`PhysicsScene` SHALL continue to own GPU buffers for collision filter data (`shape_filter_offset`, `shape_filter_count`, `shape_filter_data`). The data is now populated from `CollisionShapeComDescriptor::ignore_shape_indices` during `SubmitCollisionShape`, rather than from `m_pending_filter_handles` via `ResolveCollisionFilters`. The Adaptor resolves ObjectHandles before building COM descriptors.

#### Scenario: Filter data uploaded to GPU
- **WHEN** `SyncGpuBuffers` is called after COM descriptor submission
- **THEN** `m_gpu_shape_filter_offset`, `m_gpu_shape_filter_count`, and `m_gpu_shape_filter_data` are created or updated
- **AND** their contents match the resolved indices from the COM descriptors

## REMOVED Requirements

### Requirement: PhysicsScene stores unresolved filter ObjectHandles (original version)
**Reason**: The Adaptor now stores pending descriptors including unresolved ObjectHandles. PhysicsScene only receives resolved shape indices.
**Migration**: Move the storage and resolution logic to `PhysicsAdaptor::Flush`.
