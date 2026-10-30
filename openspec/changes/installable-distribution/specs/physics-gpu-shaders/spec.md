## MODIFIED Requirements

### Requirement: Runtime SPIR-V root exposed via cmake_config.h

The build SHALL define the C/C++ macro `ENGINE_PHYSICS_SPIRV_DIR` in the generated configuration header (driven by `engine/cmake_config.h.in`). Its value SHALL be the absolute path of the physics SPIR-V root directory used by the build pipeline (i.e. `${CMAKE_BINARY_DIR}/engine/Physics/spirv`), serving as the **developer fallback** of the runtime data-root resolution chain.

At runtime, the physics SPIR-V root SHALL be resolved through the data-root resolution chain (see `runtime-data-resolution`): the `NEEDRENAME_DATA_ROOT` override, then the installed data root (`share/needrename/spirv` relative to the executable), then the `ENGINE_PHYSICS_SPIRV_DIR` fallback — where the fallback keeps the per-configuration subdirectory selection (`ENGINE_PHYSICS_SPIRV_CONFIG_SUBDIR`) because build-tree artifacts are split per configuration. The installed form SHALL NOT use a per-configuration subdirectory. The macro SHALL remain a string literal usable in C++ to construct `std::filesystem::path` instances.

Runtime physics code requiring a precompiled physics shader SHALL resolve its file location relative to the resolved physics SPIR-V root plus the source-relative path with `.spv` appended (e.g. `solver/XPBDSolver/step.comp.spv`). Runtime code MUST NOT hard-code build-tree paths or reach into source-tree shader directories.

#### Scenario: Macro is defined and consumable

- **WHEN** any engine translation unit includes the generated configuration header
- **THEN** `ENGINE_PHYSICS_SPIRV_DIR` is defined as a string literal
- **AND** `std::filesystem::path(ENGINE_PHYSICS_SPIRV_DIR)` refers to an existing directory after a successful build

#### Scenario: Runtime resolves a shader by relative path

- **WHEN** runtime code requests the SPIR-V for `solver/XPBDSolver/step.comp`
- **THEN** it loads the file at `<resolved physics SPIR-V root>/solver/XPBDSolver/step.comp.spv`

#### Scenario: Installed binary resolves physics shaders from the data root

- **WHEN** an installed executable runs with `NEEDRENAME_DATA_ROOT` unset
- **THEN** physics SPIR-V files resolve under `share/needrename/spirv/` of the install prefix
- **AND** the absolute developer fallback path is not consulted

#### Scenario: Developer build tree resolves via the fallback

- **WHEN** an executable from a developer build tree runs on the machine that built it
- **THEN** the physics SPIR-V root resolves to the build-tree directory named by `ENGINE_PHYSICS_SPIRV_DIR` including its per-configuration subdirectory

### Requirement: Physics SPIR-V loading is centralized in one helper

`engine/Physics/` SHALL provide a single shared helper that reads a physics `.spv` file given its source-relative path, resolving it against the runtime-resolved physics SPIR-V root. Every physics component that needs a precompiled shader — solver, detectors and `gpu_algorithm` classes alike — SHALL use that helper.

Component-local copies of the loading helper SHALL NOT exist, and the helper SHALL be the only place that performs the file read and the size validation.

The same module path SHALL be the shader's compute kernel identity, and the helper SHALL resolve a path to its kernel by consulting the device's kernel cache before reading the file: a module a component already requested SHALL be returned without a second read of the file.

#### Scenario: One loader serves every component

- **WHEN** `engine/Physics/` is searched for functions that read a `.spv` file from the physics SPIR-V root
- **THEN** exactly one such helper exists
- **AND** the solver, both detectors and all `gpu_algorithm` classes call it

#### Scenario: A module two components share is read once

- **WHEN** two physics components request the kernel for the same module path
- **THEN** both resolve to the same kernel
- **AND** the file is read only for the request that created the kernel

#### Scenario: Failure diagnosis is uniform

- **WHEN** any physics component requests a missing or malformed `.spv` file
- **THEN** the same helper throws `std::runtime_error`
- **AND** its `what()` includes the absolute path attempted
