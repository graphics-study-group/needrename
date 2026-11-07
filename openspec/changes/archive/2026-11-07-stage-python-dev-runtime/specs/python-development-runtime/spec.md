## Purpose

Provide a predictable, per-configuration Python development package and one-command test and example workflows, without requiring a manual engine installation or runtime-path discovery by individual tools.

## ADDED Requirements

### Requirement: Self-contained development runtime
When Python bindings are enabled, the default build SHALL prepare a Python development runtime for the selected configuration, and a `python_runtime` target SHALL prepare it on demand. The runtime SHALL contain the package sources, compiled extension and stub, required shared libraries, built-in assets, physics SPIR-V, and project templates. Its location SHALL be stable within the active build tree and separated by configuration. After staging, setting only `PYTHONPATH` to the runtime's parent directory SHALL be sufficient to import `needrename_sim` without a manual install or `NEEDRENAME_RUNTIME_DIR` and `NEEDRENAME_DATA_ROOT` settings.

#### Scenario: Default build produces an importable runtime
- **WHEN** a developer builds the default target with Python bindings enabled
- **THEN** the selected configuration's development runtime contains all required Python, binary, and data files
- **AND** importing `needrename_sim` with only its `PYTHONPATH` configured succeeds

#### Scenario: Configurations remain isolated
- **WHEN** Debug and Release development runtimes are prepared from the same multi-config build tree
- **THEN** each runtime contains its own configuration's binaries and data
- **AND** neither runtime overwrites the other

#### Scenario: Legacy prefix is not required
- **WHEN** a fresh checkout has no `build/py-runtime` or engine install prefix
- **THEN** the Python development runtime can still be prepared entirely through its build target

### Requirement: One-command Python tests
With `ENGINE_BUILD_TESTS=ON`, the build SHALL provide a `python_test` target that depends on the selected configuration's development runtime and runs the complete Python pytest suite using the configured Python interpreter. The same suite SHALL be registered with CTest using the same staged package and environment. With `ENGINE_BUILD_TESTS=OFF`, neither the test runner target nor the CTest registration SHALL be present.

#### Scenario: Test target prepares and runs the suite
- **WHEN** a developer invokes `python_test` in a tree without a previously staged runtime
- **THEN** the runtime is built first and the complete Python test suite runs against it

#### Scenario: CTest and the convenience target agree
- **WHEN** the Python test is run through CTest or through `python_test`
- **THEN** both entry points execute the same test suite against the same configuration's staged package

#### Scenario: Tests are disabled
- **WHEN** the project is configured with `ENGINE_BUILD_TESTS=OFF`
- **THEN** no Python test target or Python CTest test is configured
- **AND** the development runtime and example targets remain available

### Requirement: One-command Python examples
The build SHALL provide separate targets for the RL loop, headless rendering, and windowed Python examples. Each target SHALL prepare the selected configuration's development runtime and invoke its example with the configured Python interpreter. The targets SHALL use the mesh-free test URDF by default, and a cache variable SHALL allow developers to select another URDF. Generated example output SHALL be written under the build tree rather than the source tree.

#### Scenario: RL example runs without path setup
- **WHEN** a developer invokes the RL example target with no additional environment setup
- **THEN** the example runs against the staged package using the default URDF

#### Scenario: Headless output stays in the build tree
- **WHEN** a developer invokes the headless example target
- **THEN** rendered frames are written beneath the selected configuration's example output directory in the build tree

#### Scenario: Developer supplies a robot
- **WHEN** the example URDF cache variable names another valid URDF
- **THEN** all Python example targets use that robot instead of the default fixture
