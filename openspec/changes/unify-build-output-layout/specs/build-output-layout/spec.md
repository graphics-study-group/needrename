## ADDED Requirements

### Requirement: Configuration-scoped build directories

The build SHALL produce, per configuration, a run directory at `<build tree>/bin/<configuration>` that
holds the linker's executables and shared libraries plus a data root staged inside it at
`share/needrename`, and an assembled product at `<build tree>/output/<configuration>`, with the
configuration name present on both single-configuration and multi-configuration generators. Import and
static libraries SHALL remain in `<build tree>/lib/<configuration>`.

#### Scenario: Multi-configuration generator

- **WHEN** the project is configured with a multi-configuration generator and built for Debug and Release
- **THEN** `bin/Debug`, `bin/Release`, `output/Debug` and `output/Release` SHALL exist
- **AND** the two configurations SHALL NOT overwrite each other's product or data

#### Scenario: Single-configuration generator

- **WHEN** the project is configured with a single-configuration generator and a build type
- **THEN** the same layout SHALL appear under one run directory name

#### Scenario: Product and linker output stay separate

- **WHEN** the development product is assembled
- **THEN** the binaries SHALL be copied out of `bin/<configuration>`
- **AND** `bin/<configuration>` SHALL NOT be inside or beneath the product directory, so an install
  into the product can never be asked to copy a file onto itself

#### Scenario: The data root is not shared between configurations

- **WHEN** two configurations are built from one multi-configuration build tree
- **THEN** each SHALL stage its data root inside its own run directory
- **AND** neither SHALL write to a directory shared with the other, because the shader form and the
  baked physics modules differ per configuration

### Requirement: The staged data root is complete and fresh

`bin/<configuration>/share/needrename` SHALL contain `builtin_assets/` in the configuration's shader
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

#### Scenario: Vulkan products carry physics modules

- **WHEN** a Vulkan configuration is built
- **THEN** its staged data root SHALL contain `spirv/` with the `.spv` modules
- **AND** a WebGPU configuration's SHALL contain the `.spv`, `.wgsl` and `.iface.json` variants

### Requirement: The build tree is runnable as built

An executable started from `<build tree>/bin/<configuration>` SHALL resolve the data root staged
inside that same directory, so a build-tree run needs no environment variable and no product
assembly.

#### Scenario: Starting a build-tree executable needs no environment

- **WHEN** an executable in `bin/<configuration>` is started from any working directory without
  `NEEDRENAME_DATA_ROOT`
- **THEN** it SHALL resolve `bin/<configuration>/share/needrename`
- **AND** the resolution step SHALL be the build-tree one, distinct from the installed one

#### Scenario: Both layouts resolve from the binary alone

- **WHEN** the same test binary runs once from `bin/<configuration>` and once from
  `output/<configuration>/bin`
- **THEN** both runs SHALL succeed and each SHALL report the resolution step for its own layout

### Requirement: The product is a runnable, install-shaped data root

`output/<configuration>` SHALL contain `bin/` and `share/needrename/` with the same subdirectories as
an installed prefix, assembled from the install rules by
`needrename_stage_runtime()`. It SHALL be a development artifact: it carries test executables and is
rebuilt by the build.

#### Scenario: Starting an executable needs no environment

- **WHEN** an executable in `output/<configuration>/bin` is started from any working directory
- **THEN** it SHALL find its data at `../share/needrename` without `NEEDRENAME_DATA_ROOT`

#### Scenario: Assembly is skipped when nothing changed

- **WHEN** the build runs and no binary and no staged category changed
- **THEN** the product SHALL NOT be reassembled

#### Scenario: One install rule, many prefixes

- **WHEN** the development product or the Python development package is assembled
- **THEN** both SHALL use the same install components and the same function a distribution install
  uses

## MODIFIED Requirements

### Requirement: Runtime data root resolution

The runtime locator SHALL resolve, in order: an existing directory named by `NEEDRENAME_DATA_ROOT`,
then `<executable directory>/../share/needrename`, then `<executable directory>/share/needrename`,
then fail with a diagnostic naming every attempted path. It SHALL NOT substitute a compile-time path,
and every data category SHALL be resolved relative to the resolved root.

#### Scenario: A build-tree product resolves like an install

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

### Requirement: Tests run the assembled product

CTest SHALL register each test executable from `output/<configuration>/bin` and SHALL clear
`NEEDRENAME_DATA_ROOT` for it, so the proximity rule is exercised rather than overridden. It SHALL
additionally register one test from `bin/<configuration>` so the build-tree layout stays covered.

#### Scenario: No test depends on an environment override

- **WHEN** the suite runs
- **THEN** no test SHALL rely on `NEEDRENAME_DATA_ROOT` being set
- **AND** the data root each test resolves SHALL be its own configuration's, from the layout it
  started in

#### Scenario: Read-only data-root test

- **WHEN** the data-root test runs
- **THEN** it SHALL NOT create or delete a directory beneath the data root it resolves
