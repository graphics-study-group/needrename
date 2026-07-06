# physics-gpu-shaders

Delta spec for the spatial-hash broad-phase change. Adds new shader directories for the broad-phase detector and reusable parallel scan. The solver now owns both broad and narrow detectors.

## MODIFIED Requirements

### Requirement: Collision detector owned and managed by solver

The `XPBDGpuSolver::Impl` SHALL own both a `SpatialHashBroadDetector` instance and a `ConvexCollisionDetector` instance, created lazily in a dedicated `EnsureCollisionDetectors` method. The broad-phase detector SHALL be constructed with `GridConfig` from `XpbdConfig` and `fallback_all_pairs_threshold`. The narrow-phase detector SHALL be constructed with `max_contacts` (derived from broad-phase pair capacity) and `contact_margin`.

`XPBDGpuSolver::AddStepPasses()` SHALL dispatch the broad-phase detector first, then feed its pair buffer and pair count into the narrow-phase detector. Both detectors SHALL be recreated if the shape count changes between frames.

`XPBDGpuSolver::AddStepPasses()` SHALL NOT accept `CollisionResultBuffers` as a parameter. The solver SHALL obtain collision result buffers internally from its owned narrow-phase detector.

#### Scenario: Solver creates both detectors on first frame

- **WHEN** `XPBDGpuSolver::AddStepPasses` is called for the first time with shapes present
- **THEN** the solver creates a `SpatialHashBroadDetector` with `GridConfig` from `XpbdConfig`
- **AND** creates a `ConvexCollisionDetector` with `max_contacts` sized for the broad-phase pair capacity
- **AND** broad-phase runs before narrow-phase in each substep

#### Scenario: External caller has no collision detector dependency

- **WHEN** an application uses `XPBDGpuSolver`
- **THEN** it does not need to create, own, or pass either detector
- **AND** the call remains `AddStepPasses(builder, physics_scene, mm_handle)`

## ADDED Requirements

### Requirement: SpatialHashBroadDetector shader source layout

Broad-phase detector GLSL source files SHALL live under `engine/Physics/shader/solver/SpatialHashBroadDetector/`. The following shaders SHALL exist:

- `compute_aabbs.comp` — per-shape AABB computation and global-shape marking with compact global list appending
- `count_cells.comp` — first pass of two-pass cell assignment
- `fill_cells.comp` — second pass of two-pass cell assignment
- `histogram_cells.comp` — counting sort histogram pass
- `scatter_sort.comp` — counting sort scatter pass
- `generate_broad_pairs.comp` — within-cell upper-triangle pair generation with collision filter checking
- `generate_global_pairs.comp` — global-shape × all-shapes pair generation via 2D dispatch
- `generate_all_pairs_fallback.comp` — all-pairs fallback for small N
- `memset_uint.comp` — clears a uint buffer to zero (reused across passes)
- `copy_uint.comp` — copies a uint buffer (used for initializing atomic counters)

#### Scenario: Broad-phase shaders compiled to SPIR-V

- **WHEN** the engine build completes
- **THEN** all 10 broad-phase shader SPIR-V files exist under `<ENGINE_PHYSICS_SPIRV_DIR>/solver/SpatialHashBroadDetector/`
- **AND** each was compiled from its corresponding `.comp` source

### Requirement: Parallel scan shader location

The reusable `parallel_scan.comp` shader SHALL live at `engine/Physics/shader/algorithm/parallel_scan.comp`. Its C++ executor class `ParallelScan` SHALL live at `engine/Physics/gpu_algorithm/ParallelScan.h`. Both SHALL be compiled/built by the existing CMake pipeline without additional configuration.

#### Scenario: Parallel scan shader compiled automatically

- **WHEN** `parallel_scan.comp` is present in the shader source tree and the engine builds
- **THEN** the SPIR-V output is produced at `<ENGINE_PHYSICS_SPIRV_DIR>/algorithm/parallel_scan.comp.spv`
- **AND** no CMakeLists.txt edits are required

### Requirement: Removed generate_pairs.comp

The file `engine/Physics/shader/solver/ConvexCollisionDetector/generate_pairs.comp` SHALL be removed. Its functionality is superseded by `SpatialHashBroadDetector` shaders.

#### Scenario: generate_pairs.comp no longer exists

- **WHEN** inspecting the source tree after this change
- **THEN** `engine/Physics/shader/solver/ConvexCollisionDetector/generate_pairs.comp` does not exist
- **AND** no `generate_pairs.comp.spv` is produced during build
