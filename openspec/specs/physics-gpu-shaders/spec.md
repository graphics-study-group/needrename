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

The pipeline SHALL be exposed as a CMake target named `physics_shader`. The `EngineLibPhysics` target and the `engine` shared library target SHALL declare a build dependency on `physics_shader` so that any successful engine build produces all physics SPIR-V artefacts.

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
- `solver/XPBDSolver/accumulate_contact_position.comp.spv`
- `solver/XPBDSolver/apply_body_position_deltas.comp.spv`
- `solver/XPBDSolver/update_velocities_from_pose.comp.spv`
- `solver/XPBDSolver/accumulate_contact_velocity.comp.spv`
- `solver/XPBDSolver/apply_body_velocity_deltas.comp.spv`
- `solver/XPBDSolver/snapshot_position.comp.spv`
- `solver/XPBDSolver/clear_int_buffer.comp.spv`
- `solver/XPBDSolver/model_matrix.comp.spv`
t- `solver/XPBDSolver/accumulate_hinge_position.comp.spv`
	- `solver/XPBDSolver/accumulate_fixed_position.comp.spv`
	- `solver/XPBDSolver/clear_hinge_lagrange.comp.spv`
	- `solver/XPBDSolver/clear_fixed_lagrange.comp.spv`

The `step.comp` placeholder SHALL be a no-op.

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
n	#### Scenario: Joint shader SPIR-V files are loaded alongside contact shaders
	- **WHEN** `EnsureInitialized()` runs
	- **THEN** all four joint shader SPIR-V files are loaded from the same directory as contact shaders
	- **AND** `ComputeStage` instances are created for each

### Requirement: XPBD solver loads and dispatches multiple compute shaders

XPBDGpuSolver SHALL load, compile, and dispatch multiple compute shader passes per `Step()` call: force integration, shape world pose update, contact position delta accumulation, body position delta application, velocity-from-pose update, contact velocity delta accumulation, body velocity delta application, buffer snapshot copies, and integer buffer clearing.

Each shader SHALL be a separate `.comp` file under `engine/Physics/shader/solver/XPBDSolver/` following the existing source layout convention.

#### Scenario: All XPBD shaders are loaded on first Step call

- **WHEN** `XPBDGpuSolver::Step` is called for the first time
- **THEN** the solver loads SPIR-V files for integrate forces, update shape world pose, accumulate/apply position deltas, update velocities, accumulate/apply velocity deltas, snapshot copy, and clear int buffer
- **AND** instantiates a `ComputeStage` for each

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

### Requirement: Build artefacts are not committed to the source tree

The physics SPIR-V output directory `${CMAKE_BINARY_DIR}/engine/Physics/spirv/` SHALL reside in the build tree only. The repository SHALL NOT contain a checked-in `engine/Physics/spirv/` directory.

#### Scenario: No spirv directory under engine/Physics in the repo
- **WHEN** inspecting the source tree at `engine/Physics/`
- **THEN** there is no `spirv/` subdirectory tracked by git
- **AND** all `.spv` artefacts live under `${CMAKE_BINARY_DIR}/...`
