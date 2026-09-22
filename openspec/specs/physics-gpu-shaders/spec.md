# physics-gpu-shaders

## Purpose

Govern the physics shader source layout, the CMake-driven GLSL→SPIR-V build pipeline, the runtime SPIR-V root exposed through `cmake_config.h`, and the contract that physics solvers load precompiled `.spv` files from this root.

## Requirements

### Requirement: Physics shader source layout

Physics GLSL source files SHALL live under `engine/Physics/shader/<group>/<solver>/<name>.<stage>`, where `<group>` is a category bucket (e.g. `solver`), `<solver>` is the algorithm-specific subdirectory (e.g. `XPBDSolver`), and `<stage>` is the shader stage extension recognised by glslang (`.comp`, `.vert`, `.frag`, `.tesc`, `.tese`, `.geom`).

Physics GLSL source code MUST NOT be embedded as string literals inside C++ source files.

#### Scenario: XPBD position step shader has a dedicated file
- **WHEN** the XPBD solver's position-update compute shader is needed
- **THEN** its GLSL source exists at `engine/Physics/shader/solver/XPBDSolver/step.comp`
- **AND** no `.cpp` file in `engine/Physics/` contains the body of that shader as a string literal

#### Scenario: XPBD model matrix shader has a dedicated file
- **WHEN** the XPBD model-matrix compute shader is needed
- **THEN** its GLSL source exists at `engine/Physics/shader/solver/XPBDSolver/model_matrix.comp`
- **AND** no `.cpp` file in `engine/Physics/` contains the body of that shader as a string literal

#### Scenario: Adding a new solver follows the same pattern
- **WHEN** a developer adds a new physics solver named `<NewSolver>` requiring shader `foo.comp`
- **THEN** they place the file at `engine/Physics/shader/solver/<NewSolver>/foo.comp`
- **AND** they do not modify any C++ source to register the file

### Requirement: CMake build pipeline compiles physics shaders to SPIR-V

The CMake build SHALL discover all GLSL source files under `engine/Physics/shader/` and compile each to SPIR-V using `Vulkan_GLSLANG_VALIDATOR_EXECUTABLE` during the build phase. Output SHALL be written to `${CMAKE_BINARY_DIR}/engine/Physics/spirv/<same-relative-path-as-source>.spv`, preserving the directory structure beneath `engine/Physics/shader/`.

The pipeline SHALL be exposed as a CMake target named `physics_shader`, declared under `engine/Physics/CMakeLists.txt`. The `EnginePhysics` target SHALL declare a build dependency on `physics_shader`, and the `engine` shared library target SHALL obtain it transitively through its link dependency on `EnginePhysics`, so that any successful engine build produces all physics SPIR-V artefacts.

Each shader file SHALL be its own incremental compilation unit: editing a single `.comp` SHALL trigger recompilation of only that file's `.spv`.

The pipeline SHALL NOT require the developer to list shader files manually in CMake. Newly added shader files SHALL be picked up on the next CMake reconfigure.

#### Scenario: Clean engine build produces all physics SPIR-V
- **WHEN** the developer performs a clean build of the `engine` target
- **THEN** for every `<rel>.<stage>` file under `engine/Physics/shader/`
- **AND** a corresponding `${CMAKE_BINARY_DIR}/engine/Physics/spirv/<rel>.<stage>.spv` exists on disk

#### Scenario: Editing one shader causes only that shader to recompile
- **WHEN** the developer modifies `engine/Physics/shader/solver/XPBDSolver/step.comp` and rebuilds
- **THEN** only `${CMAKE_BINARY_DIR}/engine/Physics/spirv/solver/XPBDSolver/step.comp.spv` is regenerated
- **AND** other physics SPIR-V files retain their previous mtime

#### Scenario: Adding a shader file requires no CMake edit
- **WHEN** the developer adds a new file `engine/Physics/shader/solver/XPBDSolver/extra.comp` and reconfigures + builds
- **THEN** `${CMAKE_BINARY_DIR}/engine/Physics/spirv/solver/XPBDSolver/extra.comp.spv` is produced
- **AND** no edit to any `CMakeLists.txt` was required

#### Scenario: GLSL syntax error fails the build
- **WHEN** a physics shader contains invalid GLSL
- **THEN** the build fails with `glslangValidator`'s diagnostic
- **AND** no stale `.spv` is left in place for that source file

#### Scenario: Engine builds physics_shader transitively
- **WHEN** the developer builds only the `engine` target
- **THEN** `physics_shader` runs before `EnginePhysics` completes
- **AND** the engine build does not declare a direct dependency on `physics_shader`

### Requirement: Runtime SPIR-V root exposed via cmake_config.h

The build SHALL define the C/C++ macro `ENGINE_PHYSICS_SPIRV_DIR` in `engine/cmake_config.h` (driven by `engine/cmake_config.h.in`). Its value SHALL be the absolute path of the physics SPIR-V root directory used by the build pipeline (i.e. `${CMAKE_BINARY_DIR}/engine/Physics/spirv`). The macro SHALL be a string literal usable in C++ to construct `std::filesystem::path` instances.

Runtime physics code requiring a precompiled physics shader SHALL resolve its file location relative to `ENGINE_PHYSICS_SPIRV_DIR` plus the source-relative path with `.spv` appended (e.g. `solver/XPBDSolver/step.comp.spv`). Runtime code MUST NOT hard-code build-tree paths or reach into source-tree shader directories.

#### Scenario: Macro is defined and consumable
- **WHEN** any engine translation unit includes `cmake_config.h`
- **THEN** `ENGINE_PHYSICS_SPIRV_DIR` is defined as a string literal
- **AND** `std::filesystem::path(ENGINE_PHYSICS_SPIRV_DIR)` refers to an existing directory after a successful build

#### Scenario: Runtime resolves a shader by relative path
- **WHEN** runtime code requests the SPIR-V for `solver/XPBDSolver/step.comp`
- **THEN** it loads the file at `<ENGINE_PHYSICS_SPIRV_DIR>/solver/XPBDSolver/step.comp.spv`

### Requirement: XPBDGpuSolver loads precompiled SPIR-V

`engine/Physics/XPBDGpuSolver.cpp` SHALL load all XPBD compute shaders by reading precompiled `.spv` files from disk and SHALL NOT invoke `ShaderCompiler::CompileGLSLtoSPV` for these shaders. The loaded `std::vector<uint32_t>` SHALL be passed to `ComputeStage::Instantiate` unchanged.

Loading SHALL occur lazily on first call to `Step()` (preserving the existing `EnsureInitialized()` behaviour). On loading failure (file missing, empty, or size not a multiple of 4 bytes), the loader SHALL throw `std::runtime_error` whose message includes the absolute path attempted.

The solver now loads the following shaders (replacing the single placeholder):
- `solver/XPBDSolver/integrate_forces.comp.spv`
- `solver/XPBDSolver/update_shape_world_pose.comp.spv`
- `solver/XPBDSolver/entries/contact_entries.comp.spv`
- `solver/XPBDSolver/entries/hinge_entries.comp.spv`
- `solver/XPBDSolver/entries/fixed_entries.comp.spv`
- `solver/XPBDSolver/accumulate_contact_position.comp.spv`
- `solver/XPBDSolver/apply_body_position_deltas.comp.spv`
- `solver/XPBDSolver/update_velocities_from_pose.comp.spv`
- `solver/XPBDSolver/accumulate_contact_velocity.comp.spv`
- `solver/XPBDSolver/apply_body_velocity_deltas.comp.spv`
- `solver/XPBDSolver/snapshot_position.comp.spv`
- `solver/XPBDSolver/clear_int_buffer.comp.spv`
- `solver/XPBDSolver/clear_entry_values.comp.spv`
- `solver/XPBDSolver/model_matrix.comp.spv`
- `solver/XPBDSolver/accumulate_hinge_position.comp.spv`
- `solver/XPBDSolver/accumulate_fixed_position.comp.spv`
- `solver/XPBDSolver/clear_hinge_lagrange.comp.spv`
- `solver/XPBDSolver/clear_fixed_lagrange.comp.spv`

There is no permutation-inversion shader: the sorted `(key, slot)` pair array produced by the radix sort is the segmented reduction's level-0 input, so `invert_permutation.comp.spv` SHALL NOT be loaded and SHALL NOT exist.

The `step.comp` placeholder SHALL be a no-op.

The `SumByKey` reduce shader (`algorithm/sum_by_key.comp.spv`) SHALL be loaded by the `SumByKey` algorithm class rather than directly by the solver, matching how `RadixSort` loads its own shaders.

`clear_entry_values.comp` SHALL be a separate shader from `clear_int_buffer.comp` rather than a mode of it: it reads the entry count from a buffer at execution time and clears `num_channels` channel planes with a stride taken from a push-constant capacity, whereas `clear_int_buffer.comp` clears a flat range whose length is a push constant. Following the solver's existing convention, the two jobs SHALL NOT be selected by a mode flag. Its dispatch geometry follows the capacity and the entry count only bounds how many elements it writes, for the same reason as the segmented reduction's level 0: the count is produced on the GPU in the same substep and is unknown when the command buffer is recorded.

#### Scenario: First Step call loads SPIR-V from disk

- **WHEN** `XPBDGpuSolver::Step` is called for the first time on a populated `PhysicsScene`
- **THEN** the solver reads all XPBD shader SPIR-V files from `<ENGINE_PHYSICS_SPIRV_DIR>/solver/XPBDSolver/`
- **AND** instantiates `ComputeStage` instances from those words

#### Scenario: No GLSL compilation occurs at runtime for XPBD shaders

- **WHEN** the engine runs an XPBD physics example end-to-end
- **THEN** `ShaderCompiler::CompileGLSLtoSPV` is not invoked from `XPBDGpuSolver` code paths

#### Scenario: Missing SPIR-V file produces a diagnostic error

- **WHEN** any XPBD solver SPIR-V file does not exist at runtime
- **AND** `XPBDGpuSolver::Step` is called
- **THEN** a `std::runtime_error` is thrown
- **AND** its `what()` includes the absolute path of the missing file

#### Scenario: Joint shader SPIR-V files are loaded alongside contact shaders

- **WHEN** `EnsureInitialized()` runs
- **THEN** all four joint shader SPIR-V files are loaded from the same directory as contact shaders
- **AND** `ComputeStage` instances are created for each

#### Scenario: No permutation inversion shader is loaded

- **WHEN** `EnsureInitialized()` runs
- **THEN** `invert_permutation.comp.spv` is not loaded and no `ComputeStage` is created for it
- **AND** the accumulated value scratch is indexed by entry slot, not by sorted position

#### Scenario: Entry-pass shaders are loaded alongside the accumulate shaders

- **WHEN** `EnsureInitialized()` runs
- **THEN** `entries/contact_entries.comp.spv`, `entries/hinge_entries.comp.spv` and `entries/fixed_entries.comp.spv` are loaded from `solver/XPBDSolver/entries/`
- **AND** a `ComputeStage` instance is created for each

#### Scenario: Counted clear shader is loaded alongside the flat clear shader

- **WHEN** `EnsureInitialized()` runs
- **THEN** `clear_entry_values.comp.spv` is loaded from `solver/XPBDSolver/`
- **AND** a `ComputeStage` instance is created for it
- **AND** `clear_int_buffer.comp.spv` is still loaded separately for the flat clears

### Requirement: XPBD solver loads and dispatches multiple compute shaders

XPBDGpuSolver SHALL load, compile, and dispatch multiple compute shader passes per `Step()` call: force integration, shape world pose update, collision detection (broad and narrow phase via the detector classes), entry-list construction (one pass per constraint type), radix sorting, accumulator clearing, contact position delta accumulation (scatter), per-body segmented reduction via `SumByKey`, body position delta application, velocity-from-pose update, contact velocity delta accumulation, body velocity delta application, buffer snapshot copies, and integer buffer clearing.

Each solver pass SHALL be a separate `.comp` file under `engine/Physics/shader/solver/XPBDSolver/` following the existing source layout convention. Algorithm shaders owned by `gpu_algorithm` classes (radix sort, `SumByKey`) SHALL live under `engine/Physics/shader/algorithm/`.

#### Scenario: All XPBD shaders are loaded on first Step call

- **WHEN** `XPBDGpuSolver::Step` is called for the first time
- **THEN** the solver loads SPIR-V files for integrate forces, update shape world pose, entry-list construction, accumulate/apply position deltas, update velocities, accumulate/apply velocity deltas, snapshot copy, clear int buffer, and the `SumByKey`/`RadixSort` algorithm stages
- **AND** instantiates a `ComputeStage` for each

#### Scenario: Algorithm shaders load through their owning classes

- **WHEN** a reduction is recorded through the `SumByKey` class
- **THEN** the solver does not load `sum_by_key.comp.spv` itself
- **AND** the `SumByKey` class instantiates its own `ComputeStage`

### Requirement: Shape world pose update shader

The system SHALL provide `update_shape_world_pose.comp` under `engine/Physics/shader/solver/XPBDSolver/` that recomputes each shape's world-space position and rotation from its owning rigid body's current pose and the shape's local offset.

The shader SHALL read `ShapeAlive`, `ShapeBoundRigidBody`, `ShapeLocalPosition`, `ShapeLocalRotation`, `RigidBodyCenterPosition`, and `RigidBodyCenterRotation`. It SHALL write `ShapeWorldPosition` and `ShapeWorldRotation`. Dead shapes SHALL be skipped. Unbound shapes SHALL copy local pose to world directly.

### Requirement: Quaternion multiplication helper

The shared GLSL header `xpbd_math.glsl` SHALL provide a `quat_mul(vec4 a, vec4 b)` function for quaternion multiplication, used by `update_shape_world_pose.comp`. The header SHALL NOT provide redundant wrappers for built-in GLSL functions (`dot`, `cross`).

### Requirement: Collision detectors owned and managed by solver

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

### Requirement: SpatialHashBroadDetector shader source layout

Broad-phase detector GLSL source files SHALL live under `engine/Physics/shader/solver/SpatialHashBroadDetector/`. The following shaders SHALL exist:

- `compute_aabbs.comp` — per-shape AABB computation and global-shape marking with compact global list appending
- `count_cells.comp` — first pass of two-pass cell assignment
- `fill_cells.comp` — second pass of two-pass cell assignment
- `histogram_cells.comp` — counting sort histogram pass
- `scatter_sort.comp` — counting sort scatter pass
- `generate_broad_pairs.comp` — within-cell upper-triangle pair generation with AABB overlap pruning and collision filter checking
- `generate_global_pairs.comp` — global-shape × all-shapes pair generation via 2D dispatch with AABB overlap pruning
- `generate_all_pairs_fallback.comp` — all-pairs fallback for small N with AABB overlap pruning
- `memset_uint.comp` — clears a uint buffer to zero (reused across passes)
- `copy_uint.comp` — copies a uint buffer (used for initializing atomic counters)

The three pair-generation shaders (`generate_broad_pairs.comp`, `generate_global_pairs.comp`, `generate_all_pairs_fallback.comp`) SHALL each bind `AabbMin` and `AabbMax` as `readonly buffer` (appended at the next free binding indices after their existing bindings) and SHALL perform a 3-axis separating-axis AABB overlap test before emitting any candidate pair. The `compute_aabbs.comp` shader (which produces these buffers) is unchanged.

#### Scenario: Broad-phase shaders compiled to SPIR-V

- **WHEN** the engine build completes
- **THEN** all 10 broad-phase shader SPIR-V files exist under `<ENGINE_PHYSICS_SPIRV_DIR>/solver/SpatialHashBroadDetector/`
- **AND** each was compiled from its corresponding `.comp` source

#### Scenario: Pair-generation shaders bind AABB buffers

- **WHEN** `generate_broad_pairs.comp`, `generate_global_pairs.comp`, or `generate_all_pairs_fallback.comp` is dispatched
- **THEN** each shader has `AabbMin` and `AabbMax` bound as `readonly buffer` at descriptor set 0
- **AND** the matching C++ pass declares `UseBuffer(aabb_min_h, RR)` and `UseBuffer(aabb_max_h, RR)` so the render graph inserts the read-after-write barrier from `compute_aabbs`

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

### Requirement: Build artefacts are not committed to the source tree

The physics SPIR-V output directory `${CMAKE_BINARY_DIR}/engine/Physics/spirv/` SHALL reside in the build tree only. The repository SHALL NOT contain a checked-in `engine/Physics/spirv/` directory.

#### Scenario: No spirv directory under engine/Physics in the repo
- **WHEN** inspecting the source tree at `engine/Physics/`
- **THEN** there is no `spirv/` subdirectory tracked by git
- **AND** all `.spv` artefacts live under `${CMAKE_BINARY_DIR}/...`
