## ADDED Requirements

### Requirement: Convex collision detector shader source layout

The convex collision detector shader source files SHALL live under `engine/Physics/shader/solver/ConvexCollisionDetector/`. The compute shaders SHALL be named `generate_pairs.comp` (GPU-side all-pairs pair generation) and `detect_collisions.comp` (MPR collision detection). Reusable GLSL header files (`mpr.glsl`, `support.glsl`, `perturbation.glsl`, `clipping.glsl`) SHALL reside in the same directory and SHALL be included via `#include` in the main detection shader.

#### Scenario: All shader files exist at expected location
- **WHEN** a developer needs the collision detection compute shaders
- **THEN** the GLSL source files exist at `engine/Physics/shader/solver/ConvexCollisionDetector/generate_pairs.comp` and `engine/Physics/shader/solver/ConvexCollisionDetector/detect_collisions.comp`

#### Scenario: GLSL headers are colocated with main shaders
- **WHEN** inspecting the collision detector shader directory
- **THEN** the files `mpr.glsl`, `support.glsl`, `perturbation.glsl`, and `clipping.glsl` exist alongside `generate_pairs.comp` and `detect_collisions.comp`

#### Scenario: CMake auto-discovers new shader files
- **WHEN** the collision detector shader files are added to `engine/Physics/shader/solver/ConvexCollisionDetector/`
- **THEN** the existing CMake `GLOB_RECURSE` automatically picks them up on the next reconfigure
- **AND** no manual CMakeLists.txt edits are required

#### Scenario: Shader compilation succeeds
- **WHEN** the engine target is built
- **THEN** `generate_pairs.comp.spv` AND `detect_collisions.comp.spv` are both produced at `<ENGINE_PHYSICS_SPIRV_DIR>/solver/ConvexCollisionDetector/`
- **AND** the build succeeds without GLSL compilation errors

### Requirement: ConvexCollisionDetector loads SPIR-V from new shader path

`ConvexCollisionDetector` SHALL load both compute shaders from `<ENGINE_PHYSICS_SPIRV_DIR>/solver/ConvexCollisionDetector/` — `generate_pairs.comp.spv` and `detect_collisions.comp.spv` — using the same `LoadPhysicsSpirv`-style pattern established by `XPBDGpuSolver`.

#### Scenario: SPIR-V files loaded from correct relative paths
- **WHEN** `ConvexCollisionDetector` initializes its compute pipelines
- **THEN** it requests the SPIR-V files at `solver/ConvexCollisionDetector/generate_pairs.comp.spv` AND `solver/ConvexCollisionDetector/detect_collisions.comp.spv`
- **AND** resolves them against `ENGINE_PHYSICS_SPIRV_DIR`

#### Scenario: Both shader files are part of physics_shader build target
- **WHEN** building the `physics_shader` target
- **THEN** both `generate_pairs.comp.spv` and `detect_collisions.comp.spv` are compiled and present in the SPIR-V output tree

## MODIFIED Requirements

### Requirement: CMake build pipeline compiles physics shaders to SPIR-V

The CMake build SHALL discover all GLSL source files under `engine/Physics/shader/` and compile each to SPIR-V using `Vulkan_GLSLANG_VALIDATOR_EXECUTABLE` during the build phase. Output SHALL be written to `${CMAKE_BINARY_DIR}/engine/Physics/spirv/<same-relative-path-as-source>.spv`, preserving the directory structure beneath `engine/Physics/shader/`.

The pipeline SHALL be exposed as a CMake target named `physics_shader`. The `EngineLibPhysics` target and the `engine` shared library target SHALL declare a build dependency on `physics_shader` so that any successful engine build produces all physics SPIR-V artefacts.

Each shader file SHALL be its own incremental compilation unit: editing a single `.comp` SHALL trigger recompilation of only that file's `.spv`.

The pipeline SHALL NOT require the developer to list shader files manually in CMake. Newly added shader files SHALL be picked up on the next CMake reconfigure.

The pipeline SHALL also support GLSL header files (`.glsl` extension) for `#include` usage. Header files SHALL be placed alongside their including `.comp` files. glslangValidator resolves `#include "..."` relative to the source file's directory by default.

#### Scenario: Clean engine build produces all physics SPIR-V
- **WHEN** the developer performs a clean build of the `engine` target
- **THEN** for every `<rel>.<stage>` file under `engine/Physics/shader/`
- **AND** a corresponding `${CMAKE_BINARY_DIR}/engine/Physics/spirv/<rel>.<stage>.spv` exists on disk

#### Scenario: Editing one shader causes only that shader to recompile
- **WHEN** the developer modifies `engine/Physics/shader/solver/XPBDSolver/step.comp` and rebuilds
- **THEN** only `${CMAKE_BINARY_DIR}/engine/Physics/spirv/solver/XPBDSolver/step.comp.spv` is regenerated
- **AND** other physics SPIR-V files retain their previous mtime

#### Scenario: Adding a shader file requires no CMake edit
- **WHEN** the developer adds new files `engine/Physics/shader/solver/ConvexCollisionDetector/generate_pairs.comp` and `engine/Physics/shader/solver/ConvexCollisionDetector/detect_collisions.comp` and reconfigures + builds
- **THEN** both `.spv` files are produced at `${CMAKE_BINARY_DIR}/engine/Physics/spirv/solver/ConvexCollisionDetector/`
- **AND** no edit to any `CMakeLists.txt` was required

#### Scenario: GLSL header includes are resolved by the compiler
- **WHEN** a compute shader contains `#extension GL_GOOGLE_include_directive : require` and `#include "mpr.glsl"`
- **AND** `mpr.glsl` resides in the same directory as the source file
- **THEN** glslangValidator compiles the shader successfully, resolving the include relative to the source directory

#### Scenario: GLSL syntax error fails the build
- **WHEN** a physics shader contains invalid GLSL
- **THEN** the build fails with `glslangValidator`'s diagnostic
- **AND** no stale `.spv` is left in place for that source file

### Requirement: Physics shader source layout

Physics GLSL source files SHALL live under `engine/Physics/shader/<group>/<solver>/<name>.<stage>`, where `<group>` is a category bucket (e.g. `solver`), `<solver>` is the algorithm-specific subdirectory (e.g. `XPBDSolver`, `ConvexCollisionDetector`), and `<stage>` is the shader stage extension recognised by glslang (`.comp`, `.vert`, `.frag`, `.tesc`, `.tese`, `.geom`). GLSL header files with the `.glsl` extension MAY also reside in these directories for `#include` reuse.

Physics GLSL source code MUST NOT be embedded as string literals inside C++ source files.

#### Scenario: XPBD position step shader has a dedicated file
- **WHEN** the XPBD solver's position-update compute shader is needed
- **THEN** its GLSL source exists at `engine/Physics/shader/solver/XPBDSolver/step.comp`
- **AND** no `.cpp` file in `engine/Physics/` contains the body of that shader as a string literal

#### Scenario: XPBD model matrix shader has a dedicated file
- **WHEN** the XPBD model-matrix compute shader is needed
- **THEN** its GLSL source exists at `engine/Physics/shader/solver/XPBDSolver/model_matrix.comp`
- **AND** no `.cpp` file in `engine/Physics/` contains the body of that shader as a string literal

#### Scenario: Collision detector shaders follow the same pattern
- **WHEN** a developer adds the convex collision detector with shaders `generate_pairs.comp`, `detect_collisions.comp`, and headers `mpr.glsl`, `support.glsl`, `perturbation.glsl`, `clipping.glsl`
- **THEN** the files reside at `engine/Physics/shader/solver/ConvexCollisionDetector/`
- **AND** no `.cpp` file in `engine/Physics/` contains shader bodies as string literals

#### Scenario: Adding a new solver follows the same pattern
- **WHEN** a developer adds a new physics solver named `<NewSolver>` requiring shader `foo.comp`
- **THEN** they place the file at `engine/Physics/shader/solver/<NewSolver>/foo.comp`
- **AND** they do not modify any C++ source to register the file
