## Context

The GPU physics engine currently has two solver patterns:

1. **Old pattern (XPBDGpuSolver)**: Does NOT inherit `ISolver`. Exposes `AddStepPasses(RenderGraphBuilder &builder, PhysicsScene &scene, RGBufferHandle external_model_matrices_handle)`. The caller-provided builder is populated with all passes — including nested CPU loops (substep, position iteration, velocity iteration) that add passes dynamically. Collision detectors also use this pattern: `AddDetectPasses(builder, scene, pre_imported_handles)`.

2. **New pattern (DummySolver)**: Inherits `ISolver`. `PreGPUStep()` handles CPU prep (uniform uploads, lazy shader init). `GPUStep(vk::CommandBuffer cb)` lazily builds its own RenderGraph via `BuildRenderGraph()` and records it via `render_graph->RecordAllPasses(cb)`. The RG is private; callers only see `GPUStep(cb)`.

The new pattern is cleaner, but XPBDGpuSolver is far more complex than DummySolver — it has collision detectors, multiple nested iteration loops, joints, and ~30 compute passes. A single RG for the entire solver would need to encode loops inside the RG (the old problem) or rebuild the RG every substep/iteration.

The solution: **multiple RGs**, each representing a distinct phase. CPU-side loops call `RecordAllPasses` on the appropriate RG the right number of times.

## Goals / Non-Goals

**Goals:**
- Refactor `XPBDGpuSolver` into `XpbdGpuSolver : ISolver` with multi-RG architecture
- Refactor collision detectors from `AddDetectPasses(builder, scene, handles)` to `Configure(...)` + `Detect(cb)` with self-owned RGs
- Keep ParallelScan as a utility function (no RG ownership)
- Correct `prev_access` on all `ImportExternalResource` calls for cross-RG synchronization
- Document the conservative prev_access strategy for loop RGs
- BroadPhase fallback path decided at RG build time

**Non-Goals:**
- Changing shader code or physics algorithms
- Changing PhysicsScene buffer layout or PhysicsGpuBuffers
- Modifying RenderGraph or RenderGraphBuilder internals
- Making ParallelScan an RG owner
- Async compute / multi-queue scheduling (deferred to future work)
- GPU-to-CPU readback in PostGPUStep (no new readback requirements)

## Decisions

### Decision 1: RG decomposition strategy

**Chosen**: 6 RGs for the solver + 1 RG per detector.

```
XpbdGpuSolver RGs:
┌──────────────────────┬──────────────────────────────────┬───────────────┐
│ RG                   │ Passes                           │ Recorded      │
├──────────────────────┼──────────────────────────────────┼───────────────┤
│ PreCollisionRG       │ Snapshots (pre-gravity,          │ 1× per substep│
│                      │ pre-contact, substep-start),     │               │
│                      │ Integrate forces,                │               │
│                      │ Update shape world poses         │               │
├──────────────────────┼──────────────────────────────────┼───────────────┤
│ PostCollisionPreIterRG│ Clear Lagrange multipliers      │ 1× per substep│
│                      │ (contact + hinge + fixed)        │               │
├──────────────────────┼──────────────────────────────────┼───────────────┤
│ PositionIterRG       │ Accum contact pos deltas,        │ N× per substep│
│                      │ Accum hinge pos deltas,          │ (re-recorded) │
│                      │ Accum fixed pos deltas,          │               │
│                      │ Apply body position deltas       │               │
├──────────────────────┼──────────────────────────────────┼───────────────┤
│ PostPositionRG       │ Update velocities from pose delta│ 1× per substep│
├──────────────────────┼──────────────────────────────────┼───────────────┤
│ VelocityIterRG       │ Accum contact velocity deltas,   │ M× per substep│
│                      │ Apply body velocity deltas       │ (re-recorded) │
├──────────────────────┼──────────────────────────────────┼───────────────┤
│ ModelMatrixRG        │ Write model matrices             │ 1× per frame  │
└──────────────────────┴──────────────────────────────────┴───────────────┘
```

**Rationale**: Each RG maps to a single loop boundary. RGs that are not re-recorded (PreCollision, PostCollision, PostPosition, ModelMatrix) use precise `prev_access`. RGs that are re-recorded (PositionIter, VelocityIter) use conservative `prev_access = RW` to ensure correctness across iterations.

**Alternatives considered**:
- Single RG per substep: Would need per-substep rebuild or loop inside RG — defeats the purpose.
- Separate RGs for substep-0 snapshots vs substep-N: Not needed — snapshots run every substep.

### Decision 2: Detector API — Configure / Detect

**Chosen**: Two-phase API replacing `AddDetectPasses`.

```cpp
class ConvexCollisionDetector {
public:
    // CPU-side prep: size buffers, upload config, cache scene & pair buffer references.
    // Called from XpbdGpuSolver::PreGPUStep().
    void Configure(
        PhysicsScene &scene,
        uint32_t max_collision_pairs,
        float contact_margin,
        const ComputeBuffer &pair_buffer,
        const ComputeBuffer &pair_count_buffer
    );

    // GPU-side: lazy-build RG, record to cb.
    // Called from XpbdGpuSolver::GPUStep() during substep loop.
    CollisionResultBuffers Detect(vk::CommandBuffer cb);
};

class SpatialHashBroadDetector {
public:
    void Configure(
        PhysicsScene &scene,
        uint32_t shape_count,
        GridConfig grid_config,
        uint32_t fallback_all_pairs_threshold
    );

    BroadDetectorOutputBuffers Detect(vk::CommandBuffer cb);
};
```

- `Configure` caches the `PhysicsScene*` reference, broad-phase pair buffer pointers (`&pair_buffer`, `&pair_count_buffer`), and all sizing parameters. It ensures internal buffers are created/resized (lazy, on first call or when sizes change). It uploads CPU data (uniforms, shape_slot_count, grid_config) to GPU-visible memory.
- `Detect` checks if the RG needs rebuilding (size changes → rebuild; fallback threshold change → different RG structure). It imports scene buffers from the cached `PhysicsScene*` into its own RG. Returns raw `*Buffers` structs (no handles — see Decision 3).

**Rationale**: Matches `ISolver`'s `PreGPUStep`/`GPUStep` split. Detector owns its RG lifecycle internally. In `PreGPUStep`, broad-phase is Configured first, then its output buffer pointers are retrieved and passed to narrow-phase Configure. This ordering ensures narrow-phase always has valid input buffer references before building its RG.

### Decision 3: No more handle forwarding between components

**Chosen**: Detectors import scene buffers into their own RG directly — no `PhysicsSceneBufferHandles` or pre-imported `RGBufferHandle` parameters on `Detect`.

Detectors cache `PhysicsScene*` from `Configure`. In `Detect`, they call `scene->GetGpuBuffers()` and `builder.ImportExternalResource(*buf, prev_access)` for every scene buffer they need. `prev_access` reflects the state left by the preceding solver RG (or previous detector RG).

The solver still needs access to detector output buffers for its subsequent RGs (e.g., narrow-phase collision results feed into position iteration). The solver obtains raw `ComputeBuffer*` from the detector's output struct, and imports these buffers into its own RGs with correct `prev_access`.

**Rationale**: Removes the `PhysicsSceneBufferHandles` coupling between solver and detectors. Each component's RG is self-contained. Handle forwarding was necessary when all passes shared one builder — with separate RGs, proper `prev_access` on `ImportExternalResource` achieves the same synchronization with simpler contracts.

**Alternative considered**: Keep handle forwarding for output buffers only. Rejected — the solver can import detector-owned buffers directly into its own RG.

### Decision 4: ParallelScan remains a utility function

**Chosen**: `ParallelScan::AddPasses(RenderGraphBuilder &builder, ...)` continues as a pass-adding function. No RG ownership.

ParallelScan owns compute stages but no data buffers. It is called during BroadPhase's RG build to add scan passes into the BroadPhase RG.

**Rationale**: ParallelScan is a pure algorithm with no data ownership. Making it an RG owner would require BroadPhase to split into 3+ RGs just to interleave scan calls, adding prev_access complexity with no architectural benefit.

### Decision 5: BroadPhase fallback at build time

**Chosen**: `SpatialHashBroadDetector::Detect` builds one of two RG structures based on `shape_count <= fallback_all_pairs_threshold`:
- **Fallback RG**: Clear global count → AABBs → Clear pair count → Fallback all-pairs
- **Spatial hash RG**: Full pipeline with ParallelScan

The decision is made at `BuildRenderGraph()` time, cached, and rebuilt only when the threshold condition changes (shape_count crosses the threshold).

**Rationale**: The two paths have different passes and buffer dependencies. Building the correct RG structure avoids dead passes and unnecessary barriers.

### Decision 6: Loop RG prev_access strategy

**Chosen**: For RGs that will be re-recorded in a loop (PositionIterRG, VelocityIterRG), all `ImportExternalResource` calls for mutable buffers use `prev_access = {AT::ShaderRandomRead, AT::ShaderRandomWrite}` ("RW") — the conservative upper bound.

```
PositionIterRG Build:
  ImportExternalResource(pos_buf, prev_access = RW)  // conservative
  ImportExternalResource(delta_buf, prev_access = RW) // conservative
  ImportExternalResource(alive_buf, prev_access = RR) // read-only, safe

  Pass 1 (Accum): pos(RR), delta(RW)
  Pass 2 (Apply): pos(RW), delta(RW)

Iteration 1: actual prev state is RR (from PostCollisionRG)
  Barrier RW→RR for pos (conservative, harmless)
  After: pos is RW, delta is RW

Iteration 2: actual prev state is RW (from Iter 1's Apply pass)
  Barrier RW→RR for pos (correct — ensures Iter 1 writes visible)
  After: pos is RW, delta is RW
  ✓ Correct for all subsequent iterations
```

**Rationale**: The baked-in `prev_access` doesn't update between `RecordAllPasses` calls. Using RW as prev_access conservatively covers both the external initial state (usually RR) and the self-loop state (RW/WW). The performance cost of an over-conservative barrier (e.g., WW→RR when actual was RR→RR) is minimal for compute-to-compute transitions.

**For non-loop RGs**: Use precise `prev_access` based on the last RG in the sequence.

### Decision 7: RG rebuild conditions

RGs are rebuilt when their structure or buffer sizes change. The Solver maintains a `GpuStateSnapshot`:

```cpp
struct GpuStateSnapshot {
    uint32_t body_count;
    uint32_t max_contacts;      // derived from shape_count * 5
    uint32_t hinge_joint_count;
    uint32_t fixed_joint_count;
    uint32_t shape_count;
};
```

| Parameter change | Affected RGs |
|------------------|--------------|
| `body_count` | All solver RGs (workgroup counts, snapshot/accumulator buffer sizes) |
| `max_contacts` | PostCollisionPreIterRG, PositionIterRG, VelocityIterRG (Lagrange/delta buffer sizes) |
| `hinge_joint_count` | PostCollisionPreIterRG, PositionIterRG (presence of hinge clear/accum passes) |
| `fixed_joint_count` | PostCollisionPreIterRG, PositionIterRG (presence of fixed clear/accum passes) |
| `shape_count` | PreCollisionRG, BroadPhase RG, NarrowPhase RG |

`substep_count`, `pos_iters`, `vel_iters` do NOT trigger rebuild — they only affect how many times RGs are recorded.

### Decision 8: Cross-RG prev_access manual tracking

Each RG build function explicitly documents the expected `prev_access` for every imported buffer. The sequence is traced manually in `GPUStep()`:

```
Phase                      pos_buf state after    next RG's prev_access for pos
────────────────────────── ────────────────────   ──────────────────────────────
Start of frame             Whatever (initial)     {AT::None} (first RG in chain)
PreCollisionRG             RW                     RW
BroadPhase Detect          RR                     RW (BroadPhase reads body pose)
NarrowPhase Detect         RR                     RR (NarrowPhase reads body pose)
PostCollisionPreIterRG     RR (unchanged)         RR
PositionIterRG (loop)      RW                     RW (conservative for loop)
PostPositionRG             RR                     RW
VelocityIterRG (loop)      RR                     RR (VelocityIter doesn't write pos)
ModelMatrixRG              RR                     RR
```

For detector-owned buffers (collision results), the solver imports them with `prev_access = WW` (detector wrote them) before reading in PositionIterRG.

## Risks / Trade-offs

- **[Risk] Incorrect prev_access causes GPU hangs or corruption** → Mitigation: Conservative strategy (RW for loop RGs, correct tracking for linear RGs) + Vulkan validation layer synchronization checks during testing.
- **[Risk] RG rebuild too frequent** → Mitigation: Only snapshot-based rebuild triggers. Loop counts excluded. Body/joint counts change rarely after scene setup.
- **[Risk] Detector Configure/Detect split introduces state inconsistency** → Mitigation: `Configure` validates that `PhysicsScene*` is non-null. `Detect` asserts Configure was called if parameters changed.
- **[Trade-off] 6+ RGs vs 1 monolithic RG** → More `RecordAllPasses` calls, more RG objects to manage, but cleaner separation, simpler rebuild logic, and testable independently.
