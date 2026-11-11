## MODIFIED Requirements

### Requirement: CMake build pipeline compiles physics shaders to SPIR-V

The CMake build SHALL discover all GLSL source files under `engine/Physics/shader/` and compile each to SPIR-V using the same native C++ `shader_baker` used for built-in shader baking. The build SHALL NOT require a separately installed `glslangValidator` executable. Output SHALL preserve the source-relative directory structure beneath the configuration-specific `${CMAKE_BINARY_DIR}/engine/Physics/spirv/` tree.

For a WebGPU build, each source SHALL also produce its WGSL variant and interface description as per-source outputs. These side artifacts SHALL NOT be represented by a directory-wide stamp.

The pipeline SHALL be exposed as a CMake target named `physics_shader`, declared under `engine/Physics/CMakeLists.txt`. The `EnginePhysics` target SHALL declare a build dependency on `physics_shader`, and the engine aggregate target SHALL obtain it transitively through its link dependency on `EnginePhysics`, so that any successful engine build produces all physics shader artifacts required by the selected backend.

Each shader file SHALL be its own incremental compilation unit: editing a single source or one of its included GLSL files SHALL trigger regeneration of only the affected source's SPIR-V and backend-specific side artifacts. Every output SHALL depend on its source, shared includes, and the native baker; WebGPU outputs SHALL also depend on the selected converter. Newly added shader files SHALL be picked up on the next CMake reconfigure without a manual file list or stamp deletion.

#### Scenario: Clean engine build produces all physics SPIR-V

- **WHEN** the developer performs a clean build of the engine target
- **THEN** every physics GLSL source has a corresponding configuration-specific `.spv` output at its source-relative path
- **AND** a WebGPU build also has the corresponding `.wgsl` and interface-description outputs
- **AND** no system `glslangValidator` is invoked

#### Scenario: Editing one shader causes only that shader to recompile

- **WHEN** the developer modifies one physics shader or a shared GLSL file and rebuilds
- **THEN** only outputs belonging to sources affected by that edit are regenerated
- **AND** unrelated physics SPIR-V, WGSL, and interface files retain their previous mtimes

#### Scenario: Adding a shader file requires no CMake edit

- **WHEN** the developer adds a new physics GLSL source and reconfigures and builds a WebGPU target
- **THEN** its SPIR-V, WGSL, and interface description are produced
- **AND** no edit to a CMake source list or deletion of `.wgsl_stamp` is required

#### Scenario: GLSL syntax error fails the build

- **WHEN** a physics shader contains invalid GLSL
- **THEN** the build fails with the baker's compiler diagnostic identifying the source
- **AND** no stale output from the failed invocation is accepted as up to date

#### Scenario: Engine builds physics_shader transitively

- **WHEN** the developer builds only the engine aggregate target
- **THEN** `physics_shader` and the native baker run before `EnginePhysics` completes
- **AND** the engine aggregate target does not declare a direct dependency on `physics_shader`
