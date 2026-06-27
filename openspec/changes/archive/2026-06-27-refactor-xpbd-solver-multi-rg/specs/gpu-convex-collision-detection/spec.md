# gpu-convex-collision-detection

## MODIFIED Requirements

### Requirement: ConvexCollisionDetector owns GPU collision detection pipeline

The `ConvexCollisionDetector` class SHALL own a compute shader pipeline for GPU narrow-phase convex collision detection using the MPR algorithm. It SHALL follow the new detector pattern: lazy SPIR-V loading on first `Detect` call (or during `Configure`), `ComputeStage` ownership, `ComputeResourceBinding` management, and self-owned RenderGraph recording via a `Detect(vk::CommandBuffer cb)` method.

The constructor SHALL accept a `RenderSystem &` only. Sizing parameters (`max_collision_pairs`, `contact_margin`) SHALL be passed to `Configure()`. No GPU resources SHALL be allocated until `Configure` or `Detect` is first called.

The detector SHALL expose a two-phase API:
```cpp
void Configure(
    PhysicsScene &scene,
    uint32_t max_collision_pairs,
    float contact_margin,
    const ComputeBuffer &pair_buffer,
    const ComputeBuffer &pair_count_buffer
);
CollisionResultBuffers Detect(vk::CommandBuffer cb);
```

`Configure` SHALL cache `&scene`, `&pair_buffer`, and `&pair_count_buffer`; ensure internal result buffers are sized; and upload `contact_margin` to a host-visible GPU uniform buffer. `Detect` SHALL lazily build the detector's own RenderGraph, import scene buffers from the cached `PhysicsScene*` and pair buffers from cached pointers with correct `prev_access`, record passes to `cb`, and return raw `ComputeBuffer*` references to collision result buffers.

#### Scenario: Lazy initialization on first Detect call

- **WHEN** `ConvexCollisionDetector::Detect(cb)` is called for the first time after `Configure`
- **THEN** the detector loads the precompiled narrow-phase SPIR-V from `<ENGINE_PHYSICS_SPIRV_DIR>/solver/ConvexCollisionDetector/detect_collisions.comp.spv`
- **AND** creates a `ComputeStage` and `ComputeResourceBinding`
- **AND** builds its RenderGraph and records it to `cb`
- **AND** subsequent calls reuse the same pipeline and RG

#### Scenario: Missing SPIR-V produces error

- **WHEN** the collision detection SPIR-V file does not exist at runtime
- **AND** `Detect(cb)` is called
- **THEN** a `std::runtime_error` is thrown with the absolute path in the error message

#### Scenario: Detect integrates with render graph using self-imported scene buffers

- **WHEN** `Detect(cb)` is called
- **THEN** the detector creates a `RenderGraphBuilder`, calls `ImportExternalResource` for each required PhysicsScene buffer (using correct `prev_access`), imports its internal result buffers, adds clear and detect passes, builds the RG, and records it to `cb`
- **AND** the detect pass dispatches `(max_collision_pairs + 63) / 64` workgroups

## REMOVED Requirements

### Requirement: ConvexCollisionDetector accepts contact margin configuration

**Reason**: `contact_margin` is now passed via `Configure()` instead of the constructor.

**Migration**: Call `detector.Configure(scene, max_pairs, contact_margin)` instead of `ConvexCollisionDetector(rs, max_pairs, contact_margin)`.

### Requirement: Detector config GPU uniform buffer

**Reason**: The uniform buffer creation and upload are now handled internally by `Configure()`.

**Migration**: No action needed — `Configure()` handles this transparently.
