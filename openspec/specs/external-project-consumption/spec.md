# external-project-consumption

## Purpose

Lets projects outside this repository build against the installed engine the way the in-tree examples do: link exported CMake targets, include the engine's headers, and generate reflection code for custom components from the SDK's shipped parser and type lists.

## Requirements

### Requirement: CMake package with exported targets

The `sdk` install component SHALL provide a CMake config package discoverable with `find_package(needrename)`. It SHALL export the engine aggregate target and the module shared-library targets under the `needrename::` namespace (e.g. `needrename::Engine`, module targets, and the editor library target), such that a consumer links exactly what the in-tree examples link. The package SHALL locate its external dependencies (SDL3, Vulkan) via dependency discovery rather than assuming the build machine's layout.

#### Scenario: External project links the engine

- **WHEN** an external project runs `find_package(needrename CONFIG REQUIRED)` and links `needrename::Engine`
- **THEN** its targets receive the engine include directories, module libraries, and transitive dependencies
- **AND** building the external project requires no paths from the engine source tree

#### Scenario: Dependencies resolved on the consumer machine

- **WHEN** the config package is loaded on a machine with SDL3 and the Vulkan SDK discoverable
- **THEN** dependency discovery succeeds and the imported targets are fully usable

### Requirement: SDK ships headers

The `sdk` component SHALL install the engine's public headers, the reflection library headers, and the vendored third-party headers that engine public headers expose (math, JSON, and similar). The installed header set SHALL be sufficient to compile a consumer that includes engine headers as the in-tree examples do.

#### Scenario: Consumer compiles against installed headers

- **WHEN** an external source file includes engine component and reflection headers
- **THEN** it compiles against the installed header set alone
- **AND** no engine source-tree include directory is referenced

### Requirement: External reflection generation

The `sdk` component SHALL provide `needrename_add_reflection()`, a CMake function that runs the shipped reflection parser over consumer-designated headers and generates the registration and serialization code for the consumer's reflected types. It SHALL supply the engine's known-type lists (the per-module reflection cache artifacts installed under the data root) as parent type information so consumer types can derive from and reference engine types.

Generated code SHALL be produced into the consumer's own build/source area, and consumer reflected components SHALL serialize and register exactly like in-tree examples' custom components.

#### Scenario: Custom component with reflection macros builds externally

- **WHEN** an external project declares a component using the reflection macros and calls `needrename_add_reflection()` on its headers
- **THEN** the reflection parser runs at the external project's build time and generated code is included in the consumer target
- **AND** the component serializes to and from the engine's archive format

#### Scenario: Consumer types derive from engine types

- **WHEN** a consumer's reflected class derives from an engine component type registered by an engine module
- **THEN** generation recognizes the engine type as known (from the shipped type lists)
- **AND** no engine header is re-scanned to produce that knowledge

#### Scenario: No engine source tree needed at consumer build time

- **WHEN** an external project builds with the SDK installed and the engine source tree unavailable
- **THEN** reflection generation completes successfully using the shipped parser and type lists

### Requirement: Package identity is the rename placeholder

The package, config file, and CMake namespace SHALL use the placeholder name `needrename`, and the points that must change on a future rename (project name, package/config name, namespace prefix) SHALL be marked in the build files.

#### Scenario: Placeholder name used consistently

- **WHEN** the installed SDK is inspected
- **THEN** the config package directory and file names use `needrename`
- **AND** exported targets use the `needrename::` namespace
