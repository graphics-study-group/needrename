# collision-filtering

## Purpose

Govern per-shape collision ignore lists: `CollisionShapeComponent` declares ignored shapes via `ComponentHandle` arrays, `PhysicsAdaptor` manages and resolves filter declarations during `Flush`, GPU filter data uses a fixed-stride buffer with greedy symmetric allocation, and broad-phase pair generation checks filters via linear scan.

## Requirements

### Requirement: CollisionShapeComponent stores ignore list

`CollisionShapeComponent` SHALL have a reflected member `std::vector<ComponentHandle> m_ignore_collision_shapes` that stores the ComponentHandles of `CollisionShapeComponent`s that should not collide with this shape. Each ComponentHandle in the array SHALL refer to a `CollisionShapeComponent`. The field SHALL be serializable and editable in the editor.

#### Scenario: Empty ignore list by default

- **WHEN** a `CollisionShapeComponent` is default-constructed
- **THEN** `m_ignore_collision_shapes` is an empty vector

#### Scenario: Ignore list serialized

- **WHEN** a scene containing a `CollisionShapeComponent` with `m_ignore_collision_shapes = [comp_A, comp_B]` is saved and loaded
- **THEN** the loaded component has the same two ComponentHandles in its ignore list

#### Scenario: Multiple shapes on same GameObject distinguished

- **WHEN** a GameObject has two `CollisionShapeComponent`s with handles `comp_A` and `comp_B`
- **AND** another component sets `m_ignore_collision_shapes = [comp_A]`
- **THEN** only `comp_A` is ignored; `comp_B` still collides normally

### Requirement: PhysicsAdaptor manages filter declarations

`PhysicsAdaptor` SHALL maintain `m_filter_map: unordered_map<ComponentHandle, unordered_set<ComponentHandle>>` storing each component's declared ignore targets. When a `CollisionShapeComponent` submits its descriptor during `Init`, `PhysicsAdaptor` SHALL replace that component's entry in `m_filter_map` with the new declaration set from `CollisionShapeDescriptor::ignore_collision_shapes`. The map SHALL NOT store symmetry-pushed entries — each entry represents only the declaring component's own intent.

#### Scenario: Filter declaration stored on Init

- **WHEN** `CollisionShapeComponent` with handle `comp_0` submits descriptor with `ignore_collision_shapes = [comp_3]`
- **THEN** `m_filter_map[comp_0] == {comp_3}`

#### Scenario: Filter declaration replaced on re-Init

- **WHEN** `m_filter_map[comp_0] == {comp_3, comp_5}`
- **AND** `comp_0` re-submits with `ignore_collision_shapes = [comp_7]`
- **THEN** `m_filter_map[comp_0] == {comp_7}` (old entries replaced, not merged)

#### Scenario: Filter declaration cleared

- **WHEN** `m_filter_map[comp_0] == {comp_3}`
- **AND** `comp_0` re-submits with `ignore_collision_shapes = []`
- **THEN** `m_filter_map[comp_0]` is empty

#### Scenario: Invalid ComponentHandle produces warning

- **WHEN** a descriptor's `ignore_collision_shapes` contains a ComponentHandle that does not refer to a `CollisionShapeComponent`
- **THEN** a warning is logged and the handle is skipped (application continues)

### Requirement: Deferred filter resolution in PhysicsAdaptor

`PhysicsAdaptor::Flush` SHALL resolve all `ComponentHandle`s in `m_filter_map` to shape indices using the existing `m_shape_component_to_index` map. For each `(src_handle, target_set)` entry:

1. Look up `src_handle` in `m_shape_component_to_index` to get `src_idx`
2. For each `tgt_handle` in `target_set`, look up `m_shape_component_to_index[tgt_handle]` to get `tgt_idx`
3. If source or target handle resolves to `INVALID_INDEX` or the target shape is not alive (`m_shape_alive[tgt_idx] == 0`), skip it with a warning

After resolution, the Adaptor derives all unordered filter pairs `{min(a,b), max(a,b)}` and applies greedy allocation (see symmetry requirement). The resulting flat filter data is submitted to `PhysicsScene::SetShapeFilters`.

#### Scenario: ComponentHandle resolved to shape index

- **WHEN** `CollisionShapeComponent` with handle `comp_B` has shape index 3
- **AND** `m_filter_map[comp_0]` contains `comp_B`
- **THEN** the pair `(0, 3)` is emitted (assuming `comp_0` maps to shape index 0)

#### Scenario: Unregistered ComponentHandle skipped

- **WHEN** `comp_B` has not been allocated a shape slot (`m_shape_component_to_index` has no entry)
- **AND** `m_filter_map[comp_0]` contains `comp_B`
- **THEN** a warning is logged and `comp_B` is skipped

#### Scenario: Non-alive target shape skipped

- **WHEN** `comp_B` maps to shape index 3 but `m_shape_alive[3] == 0`
- **THEN** the target is skipped with a warning

### Requirement: Symmetric filter data via greedy allocation

`PhysicsAdaptor::Flush` SHALL guarantee filter symmetry using greedy allocation. For each unique unordered pair `(a, b)` derived from `m_filter_map` declarations (with `a < b`), processed in sorted order, the pair is accepted only if both shape `a` and shape `b` have fewer than `MAX_FILTER_ENTRIES` entries in their per-shape filter lists. If accepted, `b` is appended to shape `a`'s list and `a` is appended to shape `b`'s list. If either side is full, the ENTIRE pair is discarded and a warning is logged.

The invariant is: shape B's index appears in shape A's GPU filter data if and only if shape A's index appears in shape B's GPU filter data. This is guaranteed because every pair is either written to both sides or skipped entirely.

#### Scenario: Bidirectional filtering from single declaration

- **WHEN** shape 0 declares `ignore = [comp_B]` and `comp_B` resolves to shape index 3
- **AND** shape 3 has no declaration targeting shape 0
- **THEN** after flush, shape 0's filter data contains 3 and shape 3's filter data contains 0

#### Scenario: Pair discarded when one side is full

- **WHEN** shape 5 already has 8 filter entries
- **AND** a new declaration creates pair `(5, 12)`
- **THEN** the pair is discarded entirely (neither 5 nor 12 receives the other in filter data)
- **AND** a warning is logged

#### Scenario: Self-reference ignored

- **WHEN** a component declares `m_ignore_collision_shapes` containing its own ComponentHandle
- **THEN** no filter pair is generated for the self-reference

### Requirement: GPU fixed-stride filter data buffer

`PhysicsScene` SHALL own a single GPU buffer for collision filter data:
- `shape_filter_data[]`: `uint` flat array with size `shape_slot_count * MAX_FILTER_ENTRIES`

The data for shape `i` is stored at `filter_data[i * MAX_FILTER_ENTRIES + k]` for `k` in `[0, MAX_FILTER_ENTRIES)`. Valid entries are sorted in ascending order and occupy the first N slots; remaining slots hold `INVALID_INDEX (0xFFFFFFFFu)`.

`PhysicsScene::SetShapeFilters(const vector<uint32_t>& filter_data, uint32_t shape_count)` SHALL replace the entire `m_shape_filter_data` with the provided data via assignment. `AllocateCollisionShapeSlot` SHALL pre-allocate `MAX_FILTER_ENTRIES` entries initialized to `INVALID_INDEX`.

#### Scenario: Filter data uploaded to GPU

- **WHEN** `RefreshGpuBuffers` is called after `SetShapeFilters`
- **THEN** `m_gpu_shape_filter_data` is created or updated
- **AND** its size equals `m_shape_alive.size() * MAX_FILTER_ENTRIES`

#### Scenario: Unused slots set to INVALID_INDEX

- **WHEN** shape 0 has 3 filter entries [3, 7, 12]
- **THEN** `filter_data[0..2]` = [3, 7, 12] and `filter_data[3..7]` = [INVALID_INDEX, ..., INVALID_INDEX]

### Requirement: GPU linear scan filter check

The broad-phase pair generation shader SHALL check filter membership using linear scan. For a candidate pair `(a, b)`, the shader SHALL iterate over `filter_data[a * MAX_FILTER_ENTRIES]` through `filter_data[a * MAX_FILTER_ENTRIES + MAX_FILTER_ENTRIES - 1]`, comparing each entry to `b`. The scan SHALL stop early on encountering `INVALID_INDEX` (end of valid entries). Since filter lists are symmetric (guaranteed by CPU), the shader SHALL only check one direction with `a = min(shape_a, shape_b)`.

`MAX_FILTER_ENTRIES` SHALL be defined as `#define MAX_FILTER_ENTRIES 8` in each broad-phase shader.

#### Scenario: Linear scan finds match

- **WHEN** `filter_data` for shape 0 contains `[3, 7, 12, INVALID, ...]` and candidate pair is (0, 7)
- **THEN** linear scan finds 7 at iteration 2
- **AND** the pair is skipped

#### Scenario: Linear scan finds no match

- **WHEN** `filter_data` for shape 0 contains `[3, 7, 12, INVALID, ...]` and candidate pair is (0, 5)
- **THEN** linear scan hits INVALID_INDEX at iteration 4
- **AND** the pair is emitted (subject to other checks)

#### Scenario: Empty filter list exits early

- **WHEN** `filter_data` for shape 0 starts with `INVALID_INDEX` at slot 0
- **AND** candidate pair is (0, 3)
- **THEN** linear scan exits at iteration 1 (first entry is INVALID)
- **AND** the pair is emitted
