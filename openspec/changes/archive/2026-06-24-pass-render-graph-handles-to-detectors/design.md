## Context

The GPU physics pipeline (`XPBDGpuSolver`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`) shares several GPU buffers from `PhysicsScene`. Currently, each component independently calls `RenderGraphBuilder::ImportExternalResource` on the same `DeviceBuffer` instances, producing distinct `RGBufferHandle` values for the same physical buffer. The `RenderGraph::BuildRenderGraph` dependency analysis groups buffer usages by handle — when the same buffer maps to different handles, the analysis sees independent resources, inserts no dependency edges, and the topological sort reorders passes arbitrarily.

Example: `shape_world_position` is written by the "XPBD Update Shape World Pose" pass and read by the "BH Compute AABBs" pass, but each imports the buffer separately. The dependency graph has no edge between these passes.

The resulting RenderDoc trace shows: AABB computation → Convex collision → Clear Lagrange → Update Shape World Pose → Fallback pairs, which is incorrect. The intended order is: Update Shape World Pose → AABBs → spatial hash → Convex collision → Clear Lagrange → position iteration → velocity iteration.

### Constraints

- `PhysicsScene` must not depend on RenderGraph types (it is a core physics module)
- Detector output buffers (pair buffer, collision results) are owned by detectors but consumed downstream — their handles must flow across component boundaries as well
- Shader bindings still require raw `ComputeBuffer&` references; `RGBufferHandle` is only for RenderGraph dependency tracking

## Goals / Non-Goals

**Goals:**
- Eliminate duplicate `ImportExternalResource` calls for the same physical buffer across solver and detector passes
- Ensure the RenderGraph dependency analysis inserts correct barriers between passes touching shared buffers
- Bundle detector outputs (buffers and their handles) into structs instead of scattered getters
- Keep `PhysicsScene.h` free of RenderGraph header dependencies

**Non-Goals:**
- Modify `RenderGraphBuilder::ImportExternalResource` (no deduplication — out of scope)
- Change shader code or compute pipeline setup
- Change the XPBD algorithm or collision detection logic
- Affect the rendering pipeline

**Also:**
- Rename `Step()` methods to clarify they build RenderGraph passes once, not dispatch per-frame

## Decisions

### Decision 1: Rename `Step()` to `AddStepPasses()` / `AddDetectPasses()`

The current `Step` name suggests per-frame dispatch (like `PhysicsScene::Step` or a game loop tick). In reality these methods are called exactly once during RenderGraph construction to register compute passes and declare buffer dependencies. The new names make the call-once-at-build-time semantics explicit:

| Class | Old | New |
|-------|-----|-----|
| `XPBDGpuSolver` | `Step()` | `AddStepPasses()` |
| `SpatialHashBroadDetector` | `Step()` | `AddDetectPasses()` |
| `ConvexCollisionDetector` | `Step()` | `AddDetectPasses()` |

**Rationale**: Prevents future misuse (e.g., someone calling `Step()` every frame and accumulating duplicate passes in the graph). Aligns with the existing `RenderGraphBuilder::AddPass` naming convention.

### Decision 2: Handle structs live in the module that owns the buffers

Each buffer category has an "owner" module. The handle struct for that category lives in the owner's header:

| Buffer category | Owner | Handle struct | Location |
|-----------------|-------|---------------|----------|
| PhysicsScene buffers | PhysicsScene | `PhysicsSceneBufferHandles` | `PhysicsScene.h` |
| Broad detector outputs | SpatialHashBroadDetector | `BroadDetectorOutputHandles` | `SpatialHashBroadDetector.h` |
| Narrow detector outputs | ConvexCollisionDetector | `NarrowDetectorOutputHandles` | `ConvexCollisionDetector.h` |

**Rationale**: The struct belongs where its buffer counterparts (`PhysicsGpuBuffers`, `CollisionResultBuffers`) are already defined. When someone adds a new buffer to a detector, they update both structs in the same file.

**Alternative considered**: A single `PhysicsRenderGraphHandles.h` header. Rejected — it splits buffer definitions from their handle counterparts, increasing the chance they drift apart.

### Decision 3: Forward-declare `RGBufferHandle` in `PhysicsScene.h` instead of including `RGAttachmentDesc.h`

`RGBufferHandle` is `enum class RGBufferHandle : int32_t {};` — it can be forward-declared with `enum class RGBufferHandle : int32_t;` per C++ standard (opaque enum declaration is valid since C++11).

**Rationale**: `PhysicsScene.h` is included broadly. Pulling in `RGAttachmentDesc.h` would transitively pull in `TextureSubresourceView.h` and `AttachmentUtils.h` — unnecessary for most consumers.

**Risk**: Opaque enum declarations are technically a C++11 extension for scoped enums (they require a fixed underlying type to be complete). `RGBufferHandle` has `: int32_t` so the forward declaration is standard-conforming. Tested against MSVC, Clang, and GCC.

### Decision 4: Detectors return output handles from `AddDetectPasses()`; callers consume them directly

Previously:
```
broad_detector->Step(builder, scene);          // void — imports its own scene buffers
narrow_detector->Step(builder, scene, ...);    // void — imports its own scene buffers + re-imports pair buffers
coll_h = builder.ImportExternalResource(*cr.collision_ids, ...); // re-imports detector outputs
```

After:
```
auto broad_out = broad_detector->AddDetectPasses(builder, scene, scene_handles);
// broad_out.pair_buffer = handle to broad detector's collision_pairs buffer
// broad_out.pair_count  = handle to broad detector's pair_count buffer

auto narrow_out = narrow_detector->AddDetectPasses(builder, scene,
    pair_buf, pair_cnt_buf, scene_handles,
    broad_out.pair_buffer, broad_out.pair_count);
// narrow_out.collision_ids = handle to narrow detector's collision_ids buffer

coll_h = narrow_out.collision_ids;  // no re-import!
```

**Rationale**: The return value is the single source of truth for the handle. There's no way for the caller to accidentally import a different handle for the same buffer.

### Decision 5: Consolidate `SpatialHashBroadDetector` scattered getters into `GetOutputBuffers()`

Instead of three separate methods (`GetPairBuffer`, `GetPairCountBuffer`, `GetMaxPairs`), return a single struct:

```cpp
struct BroadDetectorOutputBuffers {
    const ComputeBuffer &pair_buffer;
    const ComputeBuffer &pair_count_buffer;
    uint32_t max_pairs;
};
BroadDetectorOutputBuffers GetOutputBuffers() const noexcept;
```

**Rationale**: Matches the pattern already used by `ConvexCollisionDetector::GetCollisionResultBuffers()`. Callers that need all three get a single call; the struct documents the relationship between the values.

### Decision 6: Internal solver handle struct in `XPBDGpuSolver.cpp`

The solver has ~50 local `RGBufferHandle` variables in `AddStepPasses()`. A private `SolverBufferHandles` struct (defined in the `.cpp` file only) organizes them, but this is an implementation detail — it is not exposed in the header.

**Rationale**: Keeps `AddStepPasses()` readable without polluting the solver's public interface. The struct is an internal refactoring, not an API change.

## Data Flow

```
XPBDGpuSolver::AddStepPasses(builder, scene, mm_handle)
│
├─ Import scene buffers ONCE:
│    PhysicsSceneBufferHandles scene_h;
│    scene_h.shape_world_position = builder.ImportExternalResource(...);
│    scene_h.shape_alive          = builder.ImportExternalResource(...);
│    ...
│
├─ [Pass] Update Shape World Pose
│    UseBuffer(scene_h.shape_world_position, RW)  ──── WRITE
│
├─ broad_out = broad_detector->AddDetectPasses(builder, scene, scene_h)
│    │  Uses scene_h.shape_* (READ) — same handles!
│    │  Returns { .pair_buffer = h_pairs, .pair_count = h_pcnt }
│    └─ RenderGraph sees: UpdateShape ──[shape_world_position]──→ AABBs
│                          (WRITE handle -14)                (READ handle -14)  ✅
│
├─ narrow_out = narrow_detector->AddDetectPasses(builder, scene,
│        pair_buf, pair_cnt_buf,
│        scene_h, broad_out.pair_buffer, broad_out.pair_count)
│    │  Uses scene_h.shape_* (READ) — same handles!
│    │  Uses broad_out.pair_buffer (READ) — same handle!
│    │  Returns { .collision_ids = h_ids, ... }
│    └─ RenderGraph sees: BroadPairWrite ──[pair_buffer]──→ ConvexRead  ✅
│                          UpdateShape    ──[shape_wpos]──→ ConvexRead  ✅
│
├─ [Pass] Clear Lagrange  —  solver-owned buffer, no sharing needed
│
├─ [Pass] Accum Contact Pos
│    UseBuffer(narrow_out.collision_ids, RR)     ← no re-import!
│    UseBuffer(narrow_out.collision_normals, RR)
│    └─ RenderGraph sees: ConvexWrite ──[collision_ids]──→ AccumRead  ✅
│
└─ [Pass] Apply Body Pos → Update Vel → Velocity iterations ...
```

## Risks / Trade-offs

- **Risk**: Forward-declaring `enum class RGBufferHandle` may not compile on older or non-conforming C++ compilers.
  → **Mitigation**: The project already uses C++20 (`<cmake_config.h>` with C++20 flags). Opaque enum declarations for scoped enums with fixed underlying types are standard since C++11. If issues arise, fall back to including `<RGAttachmentDesc.h>` in `PhysicsScene.h` — the transitive include cost is moderate.

- **Risk**: Method renames (`Step()` → `AddStepPasses()`/`AddDetectPasses()`) and handle struct parameters are **BREAKING** API changes for `XPBDGpuSolver`, `SpatialHashBroadDetector`, and `ConvexCollisionDetector`.
  → **Mitigation**: Both detectors are only instantiated and used by `XPBDGpuSolver`. No external callers exist outside `PhysicsExampleRenderGraphBuilder` which goes through the solver. The blast radius is contained to ~5 files.

- **Trade-off**: `PhysicsSceneBufferHandles` lives in `PhysicsScene.h` but `PhysicsScene` never uses it.
  → **Accepted**: It co-locates with `PhysicsGpuBuffers` which follows the same pattern (the struct is defined alongside the class that owns the buffers, even though the class itself doesn't read the struct). This is an established codebase convention.

## Open Questions

None — the design follows patterns already established by `PhysicsGpuBuffers` and `CollisionResultBuffers`.
