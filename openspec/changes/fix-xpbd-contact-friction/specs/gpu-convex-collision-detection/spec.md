## MODIFIED Requirements

### Requirement: ConvexCollisionDetector owns GPU collision detection pipeline

The `ConvexCollisionDetector` class SHALL own a compute shader pipeline for GPU narrow-phase convex collision detection using the MPR algorithm. It SHALL load its SPIR-V lazily on first use, own its compute pipelines and resource bindings, and record its dispatches directly to a caller-supplied command buffer through a `Record(vk::CommandBuffer cb)` method. It SHALL NOT own or build a `RenderGraph`.

The constructor SHALL accept `(Rhi::DeviceContext&)`. Sizing parameters (the contact capacity, `contact_margin`) SHALL be supplied to the detector before or at its first record. No GPU resources SHALL be allocated before the first `Record`.

The detector SHALL expose:
```cpp
void BindToScene(
    PhysicsScene &scene, const SpatialHashBroadDetector &broad_detector, uint32_t contact_capacity,
    float contact_margin
);
void Record(vk::CommandBuffer cb);
CollisionResultBuffers GetResultBuffers() const;
```

**The contact-point capacity SHALL be owned by the caller and passed in; the detector SHALL NOT derive one of its own.** The detector's result buffers SHALL be sized to the capacity it was bound with, and that same value SHALL be the write budget its shader enforces, so the buffer length and the budget are one number rather than two derivations that can disagree. The caller that also sizes its per-contact multiplier buffers to that capacity is what makes an over-budget contact unrepresentable in either component.

`BindToScene` SHALL cache the bound `PhysicsScene*`, the broad-phase source and the sizing parameters, and SHALL do nothing else: no allocation, no kernel acquisition and no buffer sizing. The detector SHALL take the broad-phase pair buffers it reads from the broad detector's live `GetResultBuffers()` at preparation time, so a growth of the broad detector's pair capacity between two steps is observed rather than a stale reference being reused. `contact_margin` SHALL reach the shader through a recorded push-constant block rather than a CPU write to a host-visible uniform buffer. `Record` SHALL size its result buffers on first use and whenever the capacity it was bound with changes.

#### Scenario: Lazy initialization on first Detect call

- **WHEN** `ConvexCollisionDetector::Record(cb)` is called for the first time
- **THEN** the detector loads the precompiled narrow-phase SPIR-V from `<ENGINE_PHYSICS_SPIRV_DIR>/collision/ConvexCollisionDetector/detect_collisions.comp.spv`
- **AND** creates its compute pipeline and resource bindings
- **AND** records its dispatches to `cb`
- **AND** subsequent calls reuse the same pipeline

#### Scenario: Missing SPIR-V produces error

- **WHEN** the collision detection SPIR-V file does not exist at runtime
- **AND** `Record(cb)` is called
- **THEN** a `std::runtime_error` is thrown with the absolute path in the error message

#### Scenario: Detect integrates with render graph using self-imported scene buffers

- **WHEN** `Record(cb)` is called
- **THEN** the detector reads the scene buffers it needs through its cached `PhysicsScene*` and its cached pair buffers, and records its clear and detect dispatches directly to `cb`
- **AND** it declares no render-graph resource and inserts no render-graph barrier
- **AND** the detect pass dispatches `(max_collision_pairs + 63) / 64` workgroups

#### Scenario: The write budget is the capacity the caller passed

- **WHEN** the detector is bound with a contact capacity and records a detection pass
- **THEN** its result buffers hold at least that many entries
- **AND** the shader's write budget is that same value
- **AND** the detector does not derive a second capacity from the broad-phase pair count

### Requirement: Collision result GPU buffers

`ConvexCollisionDetector` SHALL own and expose GPU output buffers for collision results. Each manifold contact point is a separate result entry, so all result buffers SHALL be sized to the contact capacity the detector was bound with (up to 5 points per collision pair):

- `collision_ids`: `uvec2` buffer storing `(shape_index_a, shape_index_b)` for each contact point
- `collision_normals`: `vec4` buffer storing the contact normal in world space (w = independently computed penetration depth per point)
- `contact_point_a`: `vec4` buffer storing the contact point on shape A in **shape-local space** (relative to shape A's local frame, not world space)
- `contact_point_b`: `vec4` buffer storing the contact point on shape B in **shape-local space** (relative to shape B's local frame, not world space)
- `collision_count`: single `uint` buffer holding the number of contact points **written** this substep
- `contact_cursor`: single `uint` buffer holding the contact-slot reservation cursor, private to the detection dispatch and cleared with `collision_count` before every dispatch

All buffers SHALL be separate SSBOs (SoA layout) for cache-friendly access.

The collision detection compute shader SHALL convert world-space contact points to shape-local space before writing, using the shape's world pose at detection time:
```glsl
local_pt_a = quat_inv_rotate(shape_world_rot[shape_a], world_pt_a - shape_world_pos[shape_a]);
local_pt_b = quat_inv_rotate(shape_world_rot[shape_b], world_pt_b - shape_world_pos[shape_b]);
```

**`collision_count` SHALL equal the number of points this substep wrote, and SHALL NOT exceed the configured contact-write budget.** A thread that produces N manifold points SHALL reserve only the slots it will actually write, so that the published count is a bound on the valid data: every index below the count addresses result buffers written by this substep, and no index at or above the count does.

The count is consumed as both a data extent and an upper bound by later stages: the entry pass derives the substep's entry count from it, and both contact accumulate shaders iterate over it. Reserving N slots and then dropping the writes that exceed the budget publishes a count over slots that still hold an earlier substep's geometry. A consumer iterating that count then reads stale contacts, and a consumer that accumulates an unguarded per-contact value at such an index writes past the end of a buffer sized to the contact-point capacity.

#### Scenario: Collision count starts at zero

- **WHEN** a collision detection pass begins
- **THEN** the `collision_count` buffer is reset to 0 before dispatch
- **AND** the `contact_cursor` buffer is reset to 0 before the same dispatch

#### Scenario: Results written per manifold point, up to 5 per pair

- **WHEN** a compute thread detects a collision and produces N manifold points (1-5)
- **THEN** it reserves N contiguous slots only when all N lie within the contact-write budget
- **AND** writes each point as a separate entry with its own contact positions (in shape-local space) and independently computed penetration depth
- **AND** the published count is incremented by exactly the number of points written

#### Scenario: The count never exceeds the contact-write budget

- **WHEN** the candidate pairs produce more manifold points than the configured contact-write budget admits
- **THEN** the published `collision_count` is at most that budget
- **AND** no index below the published count refers to a slot that was not written this substep

#### Scenario: A partially admissible manifold does not over-count

- **WHEN** a pair produces N manifold points and only M < N of them fall within the remaining budget
- **THEN** M slots are reserved and written
- **AND** the published count is incremented by M, so the count matches the data

#### Scenario: Contact points are in shape-local space

- **WHEN** collision detection outputs a contact point for shape A at world position `(5, 2, 1)`
- **AND** shape A has world position `(3, 2, 1)` and identity rotation
- **THEN** `contact_point_a` stores `(2, 0, 0)` (the offset from shape origin in shape-local space)

#### Scenario: Contact points account for shape rotation

- **WHEN** collision detection outputs a contact point for shape A at world position `(5, 2, 1)`
- **AND** shape A has world position `(3, 2, 1)` and is rotated 90° around Z
- **THEN** `contact_point_a` stores the world offset rotated by -90° around Z (inverse shape rotation)

## ADDED Requirements

### Requirement: Contact slot reservation is an allocation cursor plus a published bound

The detection shader SHALL carry the two roles of the collision count in two counters, per `gpu-kernel-atomics` ("An allocation cursor and a published bound are separate counters"): the `contact_cursor` buffer allocates slots with a single `atomicAdd` and is allowed to run past the contact-write budget, and `collision_count` publishes the written prefix's length, derived from the ranges actually handed out with a single monotone atomic.

The published value SHALL be the maximum, over the threads that received a usable range, of that range's end clamped to the budget. A thread whose range starts at or above the budget SHALL write nothing and SHALL contribute nothing to the published value. A thread whose range is truncated by the budget SHALL contribute the budget itself, because it is the thread that fills the prefix's last slots.

The reservation SHALL NOT be obtained by retrying a compare-and-swap, and no thread's points SHALL be dropped because an attempt budget was exhausted.

*Why two counters:* the shader needs both "hand out disjoint contiguous ranges" and "never publish more than the budget". An atomic add cannot be given a ceiling, and the read-then-decide-then-add sequence that would emulate one is not atomic — two threads can both read a count below the budget and both add, publishing a count past it and over slots no thread wrote. Deriving the published value from the ranges instead makes it exactly `min(cursor, budget)` under every interleaving.

#### Scenario: The published count is the written prefix under contention

- **WHEN** the candidate pairs want more slots than the budget admits, and the threads race for ranges
- **THEN** the published `collision_count` equals the budget
- **AND** every index below it was written by this substep, and no index at or above it was

#### Scenario: Threads that get no range do not move the count

- **WHEN** a thread's reserved range starts at or above the budget
- **THEN** it writes no contact and does not raise the published count
- **AND** the published count still covers every slot written by a thread whose range did fit
