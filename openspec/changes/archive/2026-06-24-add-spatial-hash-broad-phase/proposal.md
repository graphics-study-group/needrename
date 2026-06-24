## Why

The current `ConvexCollisionDetector` generates all N×(N−1)/2 shape pairs and runs the expensive MPR narrow-phase algorithm on every pair, every frame. For N=100 shapes this is 4,950 pairs; for N=500 it becomes ~125K pairs — most of which are spatially distant and will never collide. A spatial-hash broad-phase filter eliminates the vast majority of non-colliding pairs before they reach the narrow-phase detector, dramatically reducing GPU compute cost as shape count grows.

## What Changes

- **New `SpatialHashBroadDetector` class** — independent broad-phase detector that computes per-shape AABBs, assigns shapes to spatial grid cells via a two-pass cell-assignment scheme, sorts (cell, shape) pairs by cell ID using GPU counting sort, and generates candidate collision pairs only for shapes sharing at least one cell. Owns all its GPU buffers. Includes configurable fallback to all-pairs for small N (default threshold: 8) and a "global" flag for shapes too large for spatial hashing, paired via a dedicated `generate_global_pairs.comp` shader with 2D dispatch.

- **New GPU parallel prefix sum (`ParallelScan` class)** — reusable class at `engine/Physics/gpu_algorithm/ParallelScan.h` with `parallel_scan.comp` at `engine/Physics/shader/algorithm/parallel_scan.comp`. Uses Blelloch work-efficient scan (256 threads × 2 loads = 512 elements per workgroup). Used by the broad-phase detector for shape cell offsets and cell histogram prefix sums.

- **Refactored `ConvexCollisionDetector`** — **BREAKING**: `AddDetectPasses()` now accepts an external collision-pair buffer and pair count (GPU-resident, produced by the broad detector) plus their pre-imported render graph handles, instead of generating pairs internally. The `generate_pairs.comp` shader is removed; pair generation responsibility moves entirely to `SpatialHashBroadDetector`.

- **Collision filtering via per-shape ignore lists** — `CollisionShapeComponent` gains an `std::vector<ObjectHandle> m_ignore_collision_objects` field. On Awake, PhysicsScene resolves ObjectHandles to shape indices (deferred until all shapes are registered) and uploads sorted filter arrays to new GPU buffers. The broad-phase detector checks filter data when generating candidate pairs, skipping filtered pairs entirely. CPU guarantees symmetry: if A ignores B, B also ignores A, and all pairs satisfy `index_a < index_b`.

- **Expanded `XpbdConfig`** — adds spatial-hash parameters: grid world bounds, cell size, max cells per shape (global threshold), and fallback all-pairs threshold (default: 8).

- **`XPBDGpuSolver` owns both detectors** — constructs and wires `SpatialHashBroadDetector` → `ConvexCollisionDetector` internally. The solver's `AddStepPasses()` dispatches broad-phase first, then feeds the resulting pair buffer into narrow-phase.

## Capabilities

### New Capabilities

- `spatial-hash-broad-phase`: GPU spatial-hash broad-phase collision detection with AABB computation, two-pass cell assignment, counting sort, within-cell pair generation, global-shape handling via compact index list and dedicated 2D-dispatched shader, and configurable small-N fallback.
- `collision-filtering`: Per-shape collision ignore lists specified via ObjectHandle arrays on `CollisionShapeComponent`, resolved to sorted shape-index filter data on GPU, enforced symmetrically during broad-phase pair generation.
- `gpu-parallel-scan`: Reusable GPU parallel prefix sum (Blelloch work-efficient scan) with `ParallelScan` C++ executor class, supporting arbitrary buffer sizes, used internally by the broad-phase detector.

### Modified Capabilities

- `gpu-convex-collision-detection`: Pair buffer is now an external input (produced by broad-phase) rather than internally generated. `generate_pairs.comp` is removed. Narrow-phase MPR logic is unchanged. `AddDetectPasses()` accepts pre-imported render graph handles.
- `physics-gpu-shaders`: New shader directory `SpatialHashBroadDetector/` with 10 compute shaders. `XPBDGpuSolver` owns both broad and narrow detectors instead of just the narrow detector. `parallel_scan.comp` at `engine/Physics/shader/algorithm/`.
- `xpbd-contact-solve`: Collision detection now runs as a two-stage pipeline (broad-phase → narrow-phase) within each substep, with pair buffer passing between stages.

## Impact

- **`engine/Physics/Collision/`** — new files `SpatialHashBroadDetector.h/.cpp`; modified `ConvexCollisionDetector.h/.cpp`
- **`engine/Physics/gpu_algorithm/`** — new files `ParallelScan.h/.cpp`
- **`engine/Physics/Solver/XPBDGpuSolver.h/.cpp`** — owns both detectors, wires broad→narrow pipeline
- **`engine/Physics/PhysicsScene.h/.cpp`** — new CPU filter storage + GPU filter buffers + `ResolveCollisionFilters()`
- **`engine/Physics/shader/solver/SpatialHashBroadDetector/`** — 10 `.comp` shaders (including `generate_global_pairs.comp`, `memset_uint.comp`, `copy_uint.comp`)
- **`engine/Physics/shader/algorithm/parallel_scan.comp`** — new reusable prefix sum shader
- **`engine/Physics/shader/solver/ConvexCollisionDetector/`** — remove `generate_pairs.comp`
- **`engine/Framework/component/physics/CollisionShapeComponent.h/.cpp`** — new `m_ignore_collision_objects` field
- **`engine/__generated__/meta_engine/`** — regenerated reflection/serialization for `CollisionShapeComponent`
- **`example/physics_example/`** — updated to use new `XpbdConfig` fields and two-detector pipeline
