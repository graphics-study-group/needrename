# module-target-naming

## MODIFIED Requirements

### Requirement: Shared library targets use Engine prefix

The engine SHALL name its shared-library CMake targets with the `Engine` prefix: `EngineCore`, `EngineRhi`, `EnginePhysics`, `Engine` (the main engine library), and `EngineEditor`.

#### Scenario: Target names match the convention

- **WHEN** the CMake configuration is generated
- **THEN** targets named `EngineCore`, `EngineRhi`, `EnginePhysics`, `Engine`, and `EngineEditor` SHALL exist
- **AND** no targets named `Core`, `Rhi`, `engine`, or `editor` SHALL exist

#### Scenario: Consumers link the prefixed targets

- **WHEN** the editor, examples, and tests link the engine libraries
- **THEN** they SHALL reference `Engine`, `EngineCore`, `EngineRhi`, `EnginePhysics`, and `EngineEditor` by those names

### Requirement: DLL product names match target names

Each renamed target SHALL produce a shared library whose file name matches the target name (`EngineCore.dll`, `EngineRhi.dll`, `EnginePhysics.dll`, `Engine.dll`, `EngineEditor.dll`), and the engine post-build copy step SHALL copy the renamed `EngineCore.dll` and `EnginePhysics.dll` alongside SDL3 and ktx into the output directory.

#### Scenario: DLL artifacts appear under the new names

- **WHEN** `cmake --build --preset debug` completes
- **THEN** the build output directory SHALL contain `EngineCore.dll`, `EngineRhi.dll`, `EnginePhysics.dll`, `Engine.dll`, and `EngineEditor.dll`
- **AND** no `Core.dll`, `Rhi.dll`, `engine.dll`, or `editor.dll` SHALL be produced

#### Scenario: Post-build copy uses the renamed targets

- **WHEN** the main engine target's POST_BUILD copy command runs
- **THEN** it SHALL copy the `EngineCore` and `EnginePhysics` target files using their target names

### Requirement: Unchanged target families

The naming convention SHALL NOT alter the remaining `EngineLib*` OBJECT libraries, the `EngineDep*` interface targets, or the `meta_*` reflection parser targets; these keep their existing names. `EngineLibPhysics` SHALL NOT exist.

#### Scenario: Auxiliary target names preserved

- **WHEN** the CMake configuration is generated
- **THEN** targets named `EngineLibFramework`, `EngineLibRender`, `EngineLibUserInterface`, `EngineLibHeaderInterface`, `EngineDep*`, and `meta_core`/`meta_rhi`/`meta_physics`/`meta_engine`/`meta_editor` SHALL exist unchanged
- **AND** no target named `EngineLibPhysics` SHALL exist
