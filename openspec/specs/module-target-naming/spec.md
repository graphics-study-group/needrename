# module-target-naming Specification

## Purpose

Defines the shared-library CMake target naming convention: standalone DLL targets use the `Engine` prefix (`EngineCore`, `EngineRhi`, `Engine`, `EngineEditor`) and produce DLLs of the same names, while the `EngineLib*` OBJECT libraries, `EngineDep*` interface targets, and `meta_*` reflection targets keep their existing names.

## Requirements

### Requirement: Shared library targets use Engine prefix

The engine SHALL name its shared-library CMake targets with the `Engine` prefix: `EngineCore`, `EngineRhi`, `Engine` (the main engine library), and `EngineEditor`.

#### Scenario: Target names match the convention

- **WHEN** the CMake configuration is generated
- **THEN** targets named `EngineCore`, `EngineRhi`, `Engine`, and `EngineEditor` SHALL exist
- **AND** no targets named `Core`, `Rhi`, `engine`, or `editor` SHALL exist

#### Scenario: Consumers link the prefixed targets

- **WHEN** the editor, examples, and tests link the engine libraries
- **THEN** they SHALL reference `Engine`, `EngineCore`, `EngineRhi`, and `EngineEditor` by those names

### Requirement: DLL product names match target names

Each renamed target SHALL produce a shared library whose file name matches the target name (`EngineCore.dll`, `EngineRhi.dll`, `Engine.dll`, `EngineEditor.dll`), and the engine post-build copy step SHALL copy the renamed `EngineCore.dll` alongside SDL3 and ktx into the output directory.

#### Scenario: DLL artifacts appear under the new names

- **WHEN** `cmake --build --preset debug` completes
- **THEN** the build output directory SHALL contain `EngineCore.dll`, `EngineRhi.dll`, `Engine.dll`, and `EngineEditor.dll`
- **AND** no `Core.dll`, `Rhi.dll`, `engine.dll`, or `editor.dll` SHALL be produced

#### Scenario: Post-build copy uses the renamed target

- **WHEN** the main engine target's POST_BUILD copy command runs
- **THEN** it SHALL copy the `EngineCore` target file using its new target name

### Requirement: Unchanged target families

The naming convention SHALL NOT alter the `EngineLib*` OBJECT libraries, the `EngineDep*` interface targets, or the `meta_*` reflection parser targets; these keep their existing names.

#### Scenario: Auxiliary target names preserved

- **WHEN** the CMake configuration is generated
- **THEN** targets named `EngineLibAsset`, `EngineLibFramework`, `EngineLibPhysics`, `EngineLibRender`, `EngineLibUserInterface`, `EngineLibHeaderInterface`, `EngineDep*`, and `meta_core`/`meta_rhi`/`meta_engine`/`meta_editor` SHALL exist unchanged

### Requirement: Renaming preserves module behavior

The target and DLL renames SHALL be a pure naming change: build configuration, include paths, namespaces, and runtime behavior SHALL remain unchanged, and the build output SHALL land in the same unified `bin/` directory.

#### Scenario: Build and tests stay green after rename

- **WHEN** `cmake --build --preset debug` and `ctest --preset debug` are run after the rename
- **THEN** the build SHALL succeed and all tests SHALL pass with the same count as before the rename
