# physics-gpu-shaders

## MODIFIED Requirements

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
