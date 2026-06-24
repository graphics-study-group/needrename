# collision-filtering

## Purpose

Govern per-shape collision ignore lists: specifying ignored objects via `ObjectHandle` arrays on `CollisionShapeComponent`, deferred resolution to shape indices, symmetric GPU filter data, and binary-search filtering during broad-phase pair generation.

## Requirements

### Requirement: CollisionShapeComponent stores ignore list

`CollisionShapeComponent` SHALL have a reflected member `std::vector<ObjectHandle> m_ignore_collision_objects` that stores the ObjectHandles of GameObjects whose directly-attached `CollisionShapeComponent` should not collide with this shape. Each ObjectHandle in the array SHALL refer to a GameObject that itself has a `CollisionShapeComponent`. The field SHALL be serializable and editable in the editor.

#### Scenario: Empty ignore list by default

- **WHEN** a `CollisionShapeComponent` is default-constructed
- **THEN** `m_ignore_collision_objects` is an empty vector

#### Scenario: Ignore list serialized

- **WHEN** a scene containing a `CollisionShapeComponent` with `m_ignore_collision_objects = [handle_A, handle_B]` is saved and loaded
- **THEN** the loaded component has the same two ObjectHandles in its ignore list

### Requirement: PhysicsScene stores unresolved filter ObjectHandles

`PhysicsScene` SHALL store the raw `std::vector<ObjectHandle>` per shape upon registration. During `RegisterCollisionShape`, the ignore list from the component is stored in a pending filter handles array keyed by shape index.

#### Scenario: Filter handles stored at registration

- **WHEN** `RegisterCollisionShape` is called for a component with `m_ignore_collision_objects = [handle_X]`
- **THEN** the ObjectHandle `[handle_X]` is stored in `m_pending_filter_handles[shape_index]`

### Requirement: Deferred filter resolution

`PhysicsScene` SHALL provide a `ResolveCollisionFilters(Scene &scene)` method that resolves all pending filter ObjectHandles to shape indices. For each unresolved ObjectHandle:
1. Look up the `GameObject` in the `Scene`
2. Find the `CollisionShapeComponent` directly attached to that `GameObject` (not through rigid body ancestry). Each GameObject is expected to have exactly one `CollisionShapeComponent`.
3. Get its `GetPhysicsShapeIndex()`

As a safety measure, if the GameObject happens to have multiple `CollisionShapeComponent`s (atypical but not prevented by the engine), all of their shape indices SHALL be collected.

The method SHALL be called once after all GameObjects have been Awake'd and before the first physics step. After resolution, `m_pending_filter_handles` SHALL be cleared.

#### Scenario: ObjectHandle resolved to shape index

- **WHEN** GameObject B has a `CollisionShapeComponent` with shape index 3
- **AND** shape 0's pending filter contains `ObjectHandle_B`
- **THEN** after resolution, shape 0's filter data contains `[3]`

#### Scenario: ObjectHandle with no CollisionShapeComponent produces empty filter

- **WHEN** GameObject B has no `CollisionShapeComponent`
- **AND** shape 0's pending filter contains `ObjectHandle_B`
- **THEN** after resolution, shape 0's filter data is empty (the handle is silently ignored)

#### Scenario: Unregistered shape handle skipped

- **WHEN** GameObject B's `CollisionShapeComponent` is not yet registered (`GetPhysicsShapeIndex() == INVALID_INDEX`)
- **AND** shape 0's pending filter contains `ObjectHandle_B`
- **THEN** that handle is skipped during resolution (the target shape index is not added)

### Requirement: Symmetric filter data on CPU

`ResolveCollisionFilters` SHALL ensure symmetry: if shape A resolves to filtering shape B (from the target ObjectHandle), then shape B's filter SHALL also contain A. After building the initial per-shape filter lists from the ignore handles, a second pass SHALL add reverse references.

#### Scenario: Bidirectional filtering enforced

- **WHEN** shape 0 (from object A) resolves ObjectHandle_B to shape index 3
- **THEN** shape 3's filter contains 0 and shape 0's filter contains 3

### Requirement: GPU filter data buffers

`PhysicsScene` SHALL own GPU buffers for collision filter data:
- `shape_filter_offset[]`: `uint` per shape slot, start index in the flat filter data array
- `shape_filter_count[]`: `uint` per shape slot, number of filtered shape indices
- `shape_filter_data[]`: `uint` flat array, concatenated sorted filter lists for all shapes

For shapes with no filter entries, `shape_filter_count[i] = 0` and `shape_filter_offset[i]` SHALL point to an arbitrary valid location (its value is unused when count is zero).

#### Scenario: Filter data uploaded to GPU

- **WHEN** `RefreshGpuBuffers` is called after filter resolution
- **THEN** `m_gpu_shape_filter_offset`, `m_gpu_shape_filter_count`, and `m_gpu_shape_filter_data` are created or updated
- **AND** their contents match the resolved CPU-side filter arrays

### Requirement: Filter data cleaned on shape unregistration

When `UnregisterCollisionShape(shape_index)` is called, `PhysicsScene` SHALL remove all references to `shape_index` from all other shapes' filter data arrays and rebuild the flat GPU buffers. The unregistered shape's own filter data SHALL be cleared.

#### Scenario: Filter references removed on unregistration

- **WHEN** shape 0's filter contains {3, 7} and shape 3's filter contains {0, 5}
- **AND** shape 0 is unregistered
- **THEN** shape 3's filter is updated to {5} (reference to 0 removed)
- **AND** shape 7's filter is unchanged (0 was not in it)

### Requirement: GPU binary search filter check

The broad-phase pair generation shader SHALL check filter membership using binary search. For a candidate pair `(a, b)`, the shader SHALL search for `b` in `shape_filter_data[shape_filter_offset[a] .. shape_filter_offset[a] + shape_filter_count[a]]`. Since filter lists are sorted ascending, binary search SHALL complete in O(log K) time where K is the filter count.

Since filter lists are symmetric (guaranteed by CPU), the shader SHALL only check one direction (`b` in `a`'s list).

#### Scenario: Binary search finds match

- **WHEN** `shape_filter_data` for shape 0 contains `[3, 7, 12]` and candidate pair is (0, 7)
- **THEN** binary search finds 7 at index 1
- **AND** the pair is skipped

#### Scenario: Binary search finds no match

- **WHEN** `shape_filter_data` for shape 0 contains `[3, 7, 12]` and candidate pair is (0, 5)
- **THEN** binary search finds no match
- **AND** the pair is emitted (subject to other checks)
