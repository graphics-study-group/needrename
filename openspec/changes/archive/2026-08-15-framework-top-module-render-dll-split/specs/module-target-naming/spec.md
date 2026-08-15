# module-target-naming

## MODIFIED Requirements

### Requirement: Shared library targets use Engine prefix

The engine SHALL name its shared-library CMake targets with the `Engine` prefix: `EngineCore`, `EngineRhi`, `EngineAssetCore`, `EnginePhysics`, `EngineRender`, `EngineFramework`, and `EngineEditor`. `Engine` SHALL be an INTERFACE aggregation target (not a shared library) that links the six module DLLs.

#### Scenario: Target names match the convention

- **WHEN** the CMake configuration is generated
- **THEN** targets named `EngineCore`, `EngineRhi`, `EngineAssetCore`, `EnginePhysics`, `EngineRender`, `EngineFramework`, and `EngineEditor` SHALL exist
- **AND** the `Engine` target SHALL exist as an INTERFACE library
- **AND** no targets named `Core`, `Rhi`, `engine`, or `editor` SHALL exist

#### Scenario: Consumers link the prefixed targets

- **WHEN** the editor, examples, and tests link the engine libraries
- **THEN** they SHALL reference `Engine`, `EngineCore`, `EngineRhi`, `EngineAssetCore`, `EnginePhysics`, `EngineRender`, and `EngineFramework` by those names

### Requirement: DLL product names match target names

Each shared-library target SHALL produce a DLL whose file name matches the target name (`EngineCore.dll`, `EngineRhi.dll`, `EngineAssetCore.dll`, `EnginePhysics.dll`, `EngineRender.dll`, `EngineFramework.dll`, `EngineEditor.dll`). The `Engine` target SHALL NOT produce a DLL. The engine post-build copy step SHALL copy the module DLLs alongside SDL3 and ktx into the output directory.

#### Scenario: DLL artifacts appear under the new names

- **WHEN** `cmake --build --preset debug` completes
- **THEN** the build output directory SHALL contain `EngineCore.dll`, `EngineRhi.dll`, `EngineAssetCore.dll`, `EnginePhysics.dll`, `EngineRender.dll`, `EngineFramework.dll`, and `EngineEditor.dll`
- **AND** no `Engine.dll`, `Core.dll`, `Rhi.dll`, `engine.dll`, or `editor.dll` SHALL be produced

#### Scenario: Post-build copy uses the renamed targets

- **WHEN** the post-build copy command runs
- **THEN** it SHALL copy the `EngineCore`, `EngineAssetCore`, `EnginePhysics`, `EngineRender`, and `EngineFramework` target files using their target names

### Requirement: Unchanged target families

The naming convention SHALL NOT alter the remaining `EngineDep*` interface targets. The `EngineLib*` OBJECT library family SHALL NOT exist: `EngineLibFramework`, `EngineLibRender`, and `EngineLibUserInterface` are gone (their content moved into the new DLLs). Reflection targets SHALL be `meta_core`/`meta_rhi`/`meta_asset_core`/`meta_physics`/`meta_render`/`meta_framework`/`meta_editor`; `meta_engine` SHALL NOT exist.

#### Scenario: Auxiliary target names preserved

- **WHEN** the CMake configuration is generated
- **THEN** targets named `EngineLibHeaderInterface`, `EngineDep*`, and `meta_core`/`meta_rhi`/`meta_asset_core`/`meta_physics`/`meta_render`/`meta_framework`/`meta_editor` SHALL exist
- **AND** no target named `EngineLibFramework`, `EngineLibRender`, `EngineLibUserInterface`, or `meta_engine` SHALL exist

### Requirement: Renaming preserves module behavior

The target and DLL renames SHALL be a pure naming and boundary change: build configuration, include paths (except the documented `MainClass.h`/`GUISystem.h`/`Input.h` moves), namespaces, and runtime behavior SHALL remain unchanged, and the build output SHALL land in the same unified `bin/` directory.

#### Scenario: Build and tests stay green after rename

- **WHEN** `cmake --build --preset debug` and `ctest --preset debug` are run after the rename
- **THEN** the build SHALL succeed and all tests SHALL pass with the same count as before the rename
