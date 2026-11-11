# runtime-data-resolution

## Purpose

Replaces the machine-absolute path macros with a runtime data-root resolution chain so the same binaries work from an install prefix and from a developer build tree, and keeps executables from writing into installed or source data.

## Requirements

### Requirement: Runtime data root resolution

The runtime locator SHALL resolve, in order: an existing directory named by `NEEDRENAME_DATA_ROOT`, then `<executable directory>/../share/needrename`, then fail with a diagnostic naming every attempted path. It SHALL NOT substitute a compile-time path, and every data category SHALL be resolved relative to the resolved root.

Every runtime data location — built-in assets, project templates, physics SPIR-V, sample/test data — SHALL be expressed relative to this data root. The resolution SHALL be identical for engine internals and for executables built against the engine.

#### Scenario: Environment override wins

- **WHEN** `NEEDRENAME_DATA_ROOT` points at a valid data root
- **THEN** all data locations resolve beneath it, regardless of the layout beside the executable

#### Scenario: Installed binary resolves its own prefix

- **WHEN** an installed executable runs with `NEEDRENAME_DATA_ROOT` unset
- **THEN** the data root resolves to `../share/needrename` relative to the executable's directory

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

### Requirement: Configuration header carries no machine-absolute install paths

The generated configuration header SHALL define only layout constants (data-root subdirectory names) and SHALL carry no filesystem path at all. It SHALL be generated into the build tree, never into the source tree. The installed SDK SHALL ship a configuration header containing no absolute path of the build machine.

#### Scenario: No generated header in the source tree

- **WHEN** the repository is inspected after a configure+build
- **THEN** no generated configuration header exists under the source directories

#### Scenario: Installed header is machine-independent

- **WHEN** the SDK's installed headers are inspected
- **THEN** none contains an absolute filesystem path of the machine that built the package

### Requirement: Consuming executables use the data root for all data access

Every executable shipped or installed by this project — examples, apps, tests — SHALL locate its data (built-in assets, projects, sample content, test fixtures) through the data root resolution instead of embedding absolute source-tree or build-tree paths. Engine modules SHALL do the same for the data they own (shader includes, physics SPIR-V).

#### Scenario: No absolute build paths in shipped binaries

- **WHEN** installed executables and engine libraries are inspected for path strings
- **THEN** no developer source-tree or build-tree path appears as a runtime data location

#### Scenario: Tests locate test fixtures through the data root

- **WHEN** a test executable that needs test fixture files runs from an install (with test-data installed) or from a build tree
- **THEN** its fixtures resolve beneath the data root in both cases

### Requirement: Tests run the build tree's executables

CTest SHALL register each test executable from `output/<configuration>/bin` and SHALL clear `NEEDRENAME_DATA_ROOT` for it, so the proximity rule is exercised rather than overridden.

#### Scenario: No test depends on an environment override

- **WHEN** the suite runs
- **THEN** no test SHALL rely on `NEEDRENAME_DATA_ROOT` being set
- **AND** the data root each test resolves SHALL be its own configuration's

#### Scenario: Read-only data-root test

- **WHEN** the data-root test runs
- **THEN** it SHALL NOT create or delete a directory beneath the data root it resolves

### Requirement: Executables write only to writable locations

Shipped executables SHALL NOT write into the data root, the source tree, or any installed content. Runtime scratch work — e.g. materializing a project from a template — SHALL target a writable location such as the current working directory or a platform-appropriate user data directory.

#### Scenario: Template materialization is writable

- **WHEN** an example or app creates a working project from an installed template
- **THEN** the copy is written under a writable location
- **AND** the installed template directory is left unmodified

### Requirement: Missing data root is a clear failure

When the data root cannot be resolved or a required subdirectory is missing, the engine SHALL fail with a diagnostic that names the resolved data root location and the resolution step that produced it.

#### Scenario: Data root absent

- **WHEN** an executable runs against an install prefix whose `share/needrename` does not exist
- **THEN** startup fails with an error naming the missing data root path
