# gpu-convex-collision-detection

## MODIFIED Requirements

### Requirement: ConvexCollisionDetector owns GPU collision detection pipeline

The `ConvexCollisionDetector` class SHALL own a compute shader pipeline for GPU narrow-phase convex collision detection using the MPR algorithm. It SHALL load its SPIR-V lazily on first use, own its compute pipelines and resource bindings, and record its dispatches directly to a caller-supplied command buffer through a `Record(vk::CommandBuffer cb)` method. It SHALL NOT own or build a `RenderGraph`.

The constructor SHALL accept `(Rhi::DeviceContext&)`. Sizing parameters (`max_collision_pairs`, `contact_margin`) SHALL be supplied to the detector before or at its first record. No GPU resources SHALL be allocated before the first `Record`.

The detector SHALL expose:
```cpp
void Record(vk::CommandBuffer cb);
CollisionResultBuffers GetResultBuffers() const;
```

The detector SHALL cache the bound `PhysicsScene*` and the broad-phase pair buffers it reads from. `contact_margin` SHALL reach the shader through a recorded push-constant block rather than a CPU write to a host-visible uniform buffer. `Record` SHALL size its result buffers on first use and whenever the pair capacity it observes exceeds their capacity.

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

### Requirement: Collision pair input buffer

`ConvexCollisionDetector` SHALL receive collision pairs to test from an external GPU buffer (`uvec2` SSBO) and pair count (`uint` SSBO), both produced by the broad-phase detector. The detector SHALL NOT own or generate the pair buffer. Each pair `(index_a, index_b)` identifies two shape indices satisfying `index_a < index_b`.

CPU dispatch SHALL use `max_collision_pairs` workgroups. Threads with `gl_GlobalInvocationID.x >= pair_count` SHALL return immediately.

#### Scenario: Collision pairs are read from external GPU buffer
- **WHEN** the collision detection shader executes
- **THEN** each invocation reads one `uvec2` from the external collision pair input buffer at `gl_GlobalInvocationID.x`
- **AND** uses the two shape indices to look up shape data from PhysicsScene buffers
- **AND** threads beyond `pair_count` return immediately

#### Scenario: Pair buffer is a render-graph resource
- **WHEN** the detector records its detect pass
- **THEN** the pair buffer and pair count buffer are bound as the pass's inputs, having been produced by the broad-phase detector earlier in the same command buffer
- **AND** the barrier making the broad-phase writes visible to the detect pass has been recorded before the dispatch
- **AND** no render-graph resource or handle is involved
