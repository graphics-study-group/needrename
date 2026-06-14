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

`engine/Physics/XPBDGpuSolver.cpp` SHALL load both compute shaders by reading the precompiled `.spv` files from disk and SHALL NOT invoke `ShaderCompiler::CompileGLSLtoSPV` for these shaders. The loaded `std::vector<uint32_t>` SHALL be passed to `ComputeStage::Instantiate` unchanged.

Loading SHALL occur lazily on first call to `Step()` (preserving the existing `EnsureInitialized()` behaviour). On loading failure (file missing, empty, or size not a multiple of 4 bytes), the loader SHALL throw `std::runtime_error` whose message includes the absolute path attempted.

The functional behaviour of the GPU pipeline (per-frame Z decrement, model-matrix composition from position + rotation) SHALL be byte-for-byte equivalent to the previous inline-GLSL implementation.

#### Scenario: First Step call loads SPIR-V from disk
- **WHEN** `XPBDGpuSolver::Step` is called for the first time on a populated `PhysicsScene`
- **THEN** the solver reads `<ENGINE_PHYSICS_SPIRV_DIR>/solver/XPBDSolver/step.comp.spv`
- **AND** reads `<ENGINE_PHYSICS_SPIRV_DIR>/solver/XPBDSolver/model_matrix.comp.spv`
- **AND** instantiates two `ComputeStage` instances from those words

#### Scenario: No GLSL compilation occurs at runtime for XPBD shaders
- **WHEN** the engine runs an XPBD physics example end-to-end
- **THEN** `ShaderCompiler::CompileGLSLtoSPV` is not invoked from `XPBDGpuSolver` code paths

#### Scenario: Missing SPIR-V file produces a diagnostic error
- **WHEN** `<ENGINE_PHYSICS_SPIRV_DIR>/solver/XPBDSolver/step.comp.spv` does not exist at runtime
- **AND** `XPBDGpuSolver::Step` is called
- **THEN** a `std::runtime_error` is thrown
- **AND** its `what()` includes the absolute path of the missing file

#### Scenario: Simulation output matches the previous inline-GLSL build
- **WHEN** the physics example runs for N frames using the new SPIR-V loading path
- **THEN** the per-rigid-body Z position decreases by 0.01 per simulated step (placeholder semantics preserved)
- **AND** the model matrix buffer entries are composed of the rotation quaternion and translated position as before

### Requirement: Build artefacts are not committed to the source tree

The physics SPIR-V output directory `${CMAKE_BINARY_DIR}/engine/Physics/spirv/` SHALL reside in the build tree only. The repository SHALL NOT contain a checked-in `engine/Physics/spirv/` directory.

#### Scenario: No spirv directory under engine/Physics in the repo
- **WHEN** inspecting the source tree at `engine/Physics/`
- **THEN** there is no `spirv/` subdirectory tracked by git
- **AND** all `.spv` artefacts live under `${CMAKE_BINARY_DIR}/...`
