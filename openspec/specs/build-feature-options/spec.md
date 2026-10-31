# build-feature-options

## Purpose

Gates optional build products behind CMake feature options so the same repository can build a full developer tree, a slim engine-only tree, or an SDK, and keeps third-party subproject tests under the same policy.

## Requirements

### Requirement: Feature options gate build subsystems

The build SHALL expose cache options `ENGINE_BUILD_TESTS`, `ENGINE_BUILD_EXAMPLES`, `ENGINE_BUILD_APPS`, `ENGINE_BUILD_TOOLS`, `ENGINE_INSTALL_TESTS`, and `ENGINE_BUILD_DOCS`. Each option SHALL gate its subsystem at configure time: when an option is OFF, none of its targets are configured and none of its files are compiled.

`ENGINE_BUILD_TOOLS` gates build-time helper executables (e.g. the shader baker). `ENGINE_INSTALL_TESTS` controls whether test executables and their data participate in installation; it SHALL default to OFF regardless of `ENGINE_BUILD_TESTS`.

#### Scenario: Tests disabled

- **WHEN** `ENGINE_BUILD_TESTS=OFF` at configure time
- **THEN** no test executable targets are configured and `ctest` lists no tests
- **AND** the engine, apps, examples and tools build unaffected

#### Scenario: Examples and apps disabled

- **WHEN** `ENGINE_BUILD_EXAMPLES=OFF` and `ENGINE_BUILD_APPS=OFF`
- **THEN** no example or app targets are configured
- **AND** the engine libraries and tests still build

#### Scenario: Install-tests is independent of build-tests

- **WHEN** `ENGINE_BUILD_TESTS=ON` and `ENGINE_INSTALL_TESTS=OFF`
- **THEN** test executables build and run from the build tree
- **AND** `cmake --install` installs no test executable and no test data

### Requirement: Defaults preserve the complete developer build

All feature options except `ENGINE_INSTALL_TESTS` SHALL default to ON, so an unmodified configure of the repository builds the same set of targets as before this change.

#### Scenario: Default configure builds everything

- **WHEN** the project is configured with no option overrides
- **THEN** engine modules, apps, examples, tests and tools are all configured
- **AND** the resulting build tree behaves as it did before the options existed

### Requirement: Third-party subproject tests obey the test option

Test subprojects vendored under `third_party/` SHALL be gated by the same test policy as first-party tests. With `ENGINE_BUILD_TESTS=OFF`, configuring the project SHALL NOT create any third-party test targets.

#### Scenario: AnnoRefl tests are gated

- **WHEN** `ENGINE_BUILD_TESTS=OFF`
- **THEN** the vendored reflection library's test targets are not configured
- **AND** no test executable from `third_party/` is built

### Requirement: Minimum CMake version supports runtime dependency installation

The project SHALL declare a `cmake_minimum_required` of at least 3.21 so that runtime-dependency installation of shared libraries (e.g. bundling SDL3 next to executables) is available to the install rules.

#### Scenario: Configure on a supported CMake

- **WHEN** the project is configured with a CMake >= 3.21
- **THEN** configuration succeeds
- **AND** install rules may use runtime dependency set machinery
