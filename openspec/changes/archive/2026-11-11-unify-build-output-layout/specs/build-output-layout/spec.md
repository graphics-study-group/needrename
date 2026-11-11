## ADDED Requirements

### Requirement: A configuration's build tree is its install prefix

The build SHALL produce, per configuration, `<build tree>/output/<configuration>` laid out exactly like
an installed prefix: `bin/` holding the linker's executables, shared libraries and extension modules,
and `share/needrename/` holding the staged data root. The configuration name SHALL be present on both
single-configuration and multi-configuration generators. Import and static libraries SHALL remain in
`<build tree>/lib/<configuration>`.

#### Scenario: Multi-configuration generator

- **WHEN** the project is configured with a multi-configuration generator and built for Debug and Release
- **THEN** `output/Debug` and `output/Release` SHALL each contain `bin/` and `share/needrename/`
- **AND** neither configuration SHALL write into the other's directory

#### Scenario: Single-configuration generator

- **WHEN** the project is configured with a single-configuration generator and a build type
- **THEN** the same layout SHALL appear under one configuration name

#### Scenario: No second copy of the same bytes

- **WHEN** a configuration has been built
- **THEN** its programs SHALL exist at exactly one path in the build tree
- **AND** its data root SHALL exist at exactly one path in the build tree
- **AND** no assembly step SHALL copy either into a second tree

#### Scenario: The browser build keeps a flat output directory

- **WHEN** the project is configured for Emscripten
- **THEN** its pages and the data `--preload-file` reads SHALL remain under `<build tree>/bin`
- **AND** no prefix layout SHALL be implied for a build that is never installed

### Requirement: The staged data root is complete and fresh

`output/<configuration>/share/needrename` SHALL contain `builtin_assets/` in the configuration's shader
form, `assets/`, `projects/`, `spirv/` and `test_data/`. Each category SHALL be produced by its own
command with its own stamp and its own source-file dependencies. Physics modules SHALL be staged for
every backend.

#### Scenario: An asset edit does not re-bake shaders

- **WHEN** a file under the sample assets or the project templates changes
- **THEN** only that category SHALL be restaged
- **AND** the shader bake SHALL NOT run

#### Scenario: A no-op build stages nothing

- **WHEN** the build runs with no source change
- **THEN** no staging command SHALL run, and in particular no SPIR-V-to-WGSL conversion

#### Scenario: A deleted source leaves no copy

- **WHEN** a source file is deleted from a staged category
- **THEN** the next staging of that category SHALL NOT contain it

#### Scenario: Vulkan builds carry physics modules

- **WHEN** a Vulkan configuration is built
- **THEN** its staged data root SHALL contain `spirv/` with the `.spv` modules
- **AND** a WebGPU configuration's SHALL contain the `.spv`, `.wgsl` and `.iface.json` variants

### Requirement: Starting a build-tree program needs no environment

An executable started from `output/<configuration>/bin` SHALL resolve the data root of its own
configuration without an environment variable, through the same executable-relative step an installed
program uses, and `cmake --install` SHALL copy that same layout to any prefix.

#### Scenario: A build-tree run resolves its own data root

- **WHEN** an executable in `output/<configuration>/bin` is started from any working directory without
  `NEEDRENAME_DATA_ROOT`
- **THEN** it SHALL resolve `output/<configuration>/share/needrename`

#### Scenario: An installed run resolves the same way

- **WHEN** the same layout is installed to `<prefix>` and a program in `<prefix>/bin` is started
- **THEN** it SHALL resolve `<prefix>/share/needrename`
- **AND** the resolution step SHALL be the same one the build-tree run reported

#### Scenario: One install rule, many prefixes

- **WHEN** the Python development package is assembled
- **THEN** it SHALL use the same install components and the same
  `needrename_stage_runtime()` function a distribution install uses

## MODIFIED Requirements

### Requirement: Runtime data root resolution

The runtime locator SHALL resolve, in order: an existing directory named by `NEEDRENAME_DATA_ROOT`,
then `<executable directory>/../share/needrename`, then fail with a diagnostic naming every attempted
path. It SHALL NOT substitute a compile-time path, and every data category SHALL be resolved relative
to the resolved root.

#### Scenario: A build-tree run resolves like an install

- **WHEN** an executable in `output/<configuration>/bin` resolves its data root
- **THEN** the result SHALL be `output/<configuration>/share/needrename`
- **AND** the resolution step SHALL be the executable-relative one

#### Scenario: No root available

- **WHEN** neither an override nor an executable-relative data root exists
- **THEN** resolution SHALL throw, naming the attempted paths
- **AND** it SHALL NOT fall back to a checkout path

#### Scenario: A missing category is named

- **WHEN** a resolved root lacks a required category directory
- **THEN** resolving that category SHALL throw, naming the category, the path and the resolution step

### Requirement: Tests run the build tree's executables

CTest SHALL register each test executable from `output/<configuration>/bin` and SHALL clear
`NEEDRENAME_DATA_ROOT` for it, so the proximity rule is exercised rather than overridden.

#### Scenario: No test depends on an environment override

- **WHEN** the suite runs
- **THEN** no test SHALL rely on `NEEDRENAME_DATA_ROOT` being set
- **AND** the data root each test resolves SHALL be its own configuration's

#### Scenario: Read-only data-root test

- **WHEN** the data-root test runs
- **THEN** it SHALL NOT create or delete a directory beneath the data root it resolves
