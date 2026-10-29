## Purpose

Replaces the machine-absolute path macros with a runtime data-root resolution chain so the same binaries work from an install prefix and from a developer build tree, and keeps executables from writing into installed or source data.

## ADDED Requirements

### Requirement: Data root resolution chain

The engine SHALL resolve a single runtime data root through the first match of:

1. the `NEEDRENAME_DATA_ROOT` environment variable, if set;
2. the installed layout relative to the executable location (`../share/needrename` beneath the executable's directory);
3. a compile-time developer fallback path pointing into the developer machine's tree.

Every runtime data location — built-in assets, project templates, physics SPIR-V, sample/test data — SHALL be expressed relative to this data root. The resolution SHALL be identical for engine internals and for executables built against the engine.

#### Scenario: Environment override wins

- **WHEN** `NEEDRENAME_DATA_ROOT` points at a valid data root
- **THEN** all data locations resolve beneath it, regardless of install layout or compile-time fallback

#### Scenario: Installed binary resolves its own prefix

- **WHEN** an installed executable runs with `NEEDRENAME_DATA_ROOT` unset
- **THEN** the data root resolves to `../share/needrename` relative to the executable's directory

#### Scenario: Developer build tree still works

- **WHEN** an executable from a developer build tree runs on the machine that built it
- **THEN** the data root resolves through the compile-time fallback
- **AND** existing developer workflows (running tests from the build tree) keep working unchanged

### Requirement: Configuration header carries no machine-absolute install paths

The generated configuration header SHALL define only layout constants (data-root subdirectory names) and the developer fallback. It SHALL be generated into the build tree, never into the source tree. The installed SDK SHALL ship a configuration header containing no absolute path of the build machine.

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
