# install-layout

## Purpose

Defines the portable install prefix layout, install components, and the contract that every installed executable runs out of the box with no external path configuration.

## Requirements

### Requirement: Portable install layout

`cmake --install` SHALL produce a self-contained prefix:

- `bin/` — all executables and all shared libraries they need (engine modules, reflection library, imgui, ktx, SDL3)
- `share/needrename/` — the runtime data root, containing `builtin_assets/`, `projects/` (project templates), `spirv/` (physics SPIR-V), `reflection/` (reflection type lists), and `test_data/` (only with `ENGINE_INSTALL_TESTS=ON`)

No installed file SHALL require a path outside the prefix to run. The layout SHALL be identical for single-config (Ninja) and multi-config (MSVC) installs; a multi-config install SHALL install the artifacts of the selected configuration.

#### Scenario: Layout after install

- **WHEN** `cmake --install <build> --prefix <p>` completes
- **THEN** `<p>/bin/` contains the executables and all required shared libraries
- **AND** `<p>/share/needrename/builtin_assets/` contains the built-in asset database content
- **AND** no file outside `<p>` is required to run the installed executables

#### Scenario: Multi-config install selects one configuration

- **WHEN** `cmake --install` runs with `--config Release` on a multi-config build tree
- **THEN** the installed binaries and data are the Release configuration's artifacts

### Requirement: Installed executables run out of the box

An installed executable SHALL start and resolve all of its runtime data without environment variables, working-directory assumptions, or developer-machine paths. On Windows all required DLLs SHALL be findable in `bin/`; on Linux installed executables SHALL carry an RPATH that resolves sibling/installed libraries (e.g. `$ORIGIN`-relative) without `LD_LIBRARY_PATH`.

#### Scenario: Installed example runs from any working directory

- **WHEN** an installed example executable is launched from an unrelated working directory
- **THEN** it starts, mounts the built-in assets, and renders without path errors

#### Scenario: Missing optional data is diagnosed, not crashed on

- **WHEN** an executable that needs optional data (e.g. example data) is installed without its data component
- **THEN** it fails with a clear diagnostic naming the missing data root location
- **AND** it does not silently proceed with invalid paths

### Requirement: Install components partition the data

Installation SHALL use components: `runtime` (binaries and the required `builtin_assets/` data), `apps-data` (project templates), `examples-data` (sample content used by installed examples), `test-data` (test executables and their data, only with `ENGINE_INSTALL_TESTS=ON`), and `sdk` (development files for external projects). `assets/img/` (documentation images) SHALL NOT be installed by any component.

A `runtime`-only install SHALL be a working distribution for engine binaries plus built-in data.

#### Scenario: Runtime-only install works

- **WHEN** only the `runtime` component is installed
- **THEN** engine binaries and `share/needrename/builtin_assets/` are present
- **AND** no `assets/img/` content is present anywhere in the prefix

#### Scenario: Test data follows its switch

- **WHEN** `ENGINE_INSTALL_TESTS=OFF`
- **THEN** installing all components installs no test executable and no `test_data/`
- **WHEN** `ENGINE_INSTALL_TESTS=ON`
- **THEN** test executables install into `bin/` and their data into `share/needrename/test_data/`

### Requirement: Runtime shared libraries ship with the distribution

All shared libraries required at runtime — engine module DLLs, the reflection library, imgui, ktx, and the SDL3 runtime — SHALL be installed into `bin/` alongside the executables. The Vulkan loader and driver SHALL not be bundled; their absence SHALL be reported as a clear runtime error.

#### Scenario: SDL3 runtime is bundled

- **WHEN** installation completes on Windows
- **THEN** `bin/` contains the SDL3 shared library next to the executables

#### Scenario: No Vulkan loader bundled

- **WHEN** the installed `bin/` directory is inspected
- **THEN** no Vulkan loader or driver library is present in the distribution

### Requirement: Optional CPack packaging

The build MAY expose a CPack `package` target producing per-component or bundled ZIP archives named with the project name and version. Packaging SHALL NOT be the only way to produce a distribution; `cmake --install` SHALL remain sufficient.

#### Scenario: ZIP package produced

- **WHEN** the `package` target is built with the ZIP generator enabled
- **THEN** an archive named `needrename-<version>-<platform>.zip` is produced
- **AND** unpacking it yields the same layout as `cmake --install`
