# reflection-module Specification

## Purpose
TBD - created by archiving change extract-core-and-reflection-dlls. Update Purpose after archive.
## Requirements
### Requirement: Reflection is a standalone shared library
The engine SHALL compile Reflection as a separate `Reflection.dll` shared library that depends on no other engine module (only std, glm, and nlohmann/json).

#### Scenario: Reflection.dll has zero engine module dependencies
- **WHEN** the build system links Reflection.dll
- **THEN** Reflection.dll SHALL NOT link to Core.dll, Framework.dll, Render.dll, Asset.dll, Physics.dll, or UI.dll

#### Scenario: Reflection.dll links only external libraries
- **WHEN** the build system links Reflection.dll
- **THEN** Reflection.dll SHALL link only to glm and nlohmann/json (plus standard library)

### Requirement: Reflection exports type registry symbols
The Reflection module SHALL export its type registry (`Type`, `TypeRegistrar`, `Field`, `Method`, `Var`) and serialization (`Archive`, `serialize<T>`, `deserialize<T>`) symbols so that other DLLs can register types and serialize data.

#### Scenario: Type registration from external DLL
- **WHEN** code in Core.dll calls `Type::s_index_type_map` or `Type::s_name_index_map` static members
- **THEN** the symbols SHALL resolve correctly through DLL import/export at link time

#### Scenario: Serialization from external DLL
- **WHEN** code in Core.dll calls `Engine::Serialization::serialize(value, archive)`
- **THEN** the correct template instantiation SHALL be resolved through Reflection.dll exports

### Requirement: Reflection macros generate DLL-compatible code
The reflection code generation pipeline SHALL produce type registration and serialization code that works correctly when the generated code is compiled into a DLL separate from Reflection.dll.

#### Scenario: Core type registration links correctly
- **WHEN** `meta_core` generated registrar code for `Engine::Transform` is compiled into Core.dll
- **THEN** the `TypeRegistrar::Register_*()` functions SHALL correctly reference Reflection.dll exported symbols

#### Scenario: Core serialization links correctly
- **WHEN** `meta_core` generated serialization code for `Engine::Transform` is compiled into Core.dll
- **THEN** the `_SERIALIZATION_SAVE_()` and `_SERIALIZATION_LOAD_()` implementations SHALL correctly call Reflection.dll exported `save_to_archive<glm::vec3>()` etc.

### Requirement: Per-module code generation targets
The build system SHALL support separate `meta_<module>` reflection code generation targets per DLL, each generating type registration and serialization code only for headers owned by that module. The engine-module targets SHALL be `meta_core`, `meta_rhi`, `meta_asset_core`, `meta_physics`, `meta_render`, and `meta_framework`; the aggregate `meta_engine` target SHALL NOT exist.

#### Scenario: meta_core generates only Core types
- **WHEN** `meta_core` code generation runs
- **THEN** only headers under `engine/Core/` SHALL be scanned for reflection annotations

#### Scenario: meta_render generates only Render types
- **WHEN** `meta_render` code generation runs
- **THEN** only headers under `engine/Render/` SHALL be scanned for reflection annotations

#### Scenario: meta_framework generates Framework types including MainClass and Input
- **WHEN** `meta_framework` code generation runs
- **THEN** only headers under `engine/Framework/` SHALL be scanned
- **AND** `MainClass.h` and `Input.h` SHALL be included in the scan

#### Scenario: No aggregate engine meta target remains
- **WHEN** the CMake configuration is generated
- **THEN** no target named `meta_engine` SHALL exist

### Requirement: DLL import/export macros
The Reflection module SHALL provide `REFLECTION_API` and `CORE_API` macros for `__declspec(dllexport)` / `__declspec(dllimport)` control on Windows, transparent (empty) on other platforms.

#### Scenario: Windows DLL export
- **WHEN** building Reflection.dll on Windows
- **THEN** `REFLECTION_API` SHALL expand to `__declspec(dllexport)`

#### Scenario: Windows DLL import
- **WHEN** building Core.dll on Windows that links Reflection.dll
- **THEN** `REFLECTION_API` SHALL expand to `__declspec(dllimport)`

#### Scenario: Non-Windows platform
- **WHEN** building on Linux or macOS
- **THEN** `REFLECTION_API` and `CORE_API` SHALL expand to empty

### Requirement: Reflection provides archive serialization
The Reflection module SHALL provide `Engine::Serialization::Archive` for JSON-based serialization with type tracking (`%type` field) and support for pointer-based object graphs.

#### Scenario: Archive saves a reflected type
- **WHEN** `Archive::save(object)` is called on a REFL_SER_CLASS annotated type
- **THEN** the output JSON SHALL contain the `%type` field and all serialized member values

#### Scenario: Archive loads a reflected type
- **WHEN** `Archive::load(object)` is called on JSON with `%type` field
- **THEN** the object SHALL be deserialized with correct member values, using the backdoor constructor if needed

### Requirement: Reflection provides glm serialization
The Reflection module SHALL provide `save_to_archive` / `load_from_archive` overloads for all common glm types (vec2, vec3, vec4, quat, mat3, mat4).

#### Scenario: Serialize glm::vec3
- **WHEN** `save_to_archive(glm::vec3{1,2,3}, archive)` is called
- **THEN** the archive SHALL contain the x, y, z components with full precision

#### Scenario: Deserialize glm::quat
- **WHEN** `load_from_archive(quat, archive)` is called on valid archive data
- **THEN** the quaternion SHALL be reconstructed with correct w, x, y, z components

### Requirement: Reflection provides std type serialization
The Reflection module SHALL provide serialization templates for standard library types: std::vector, std::map, std::unordered_map, std::shared_ptr, std::unique_ptr, std::any.

#### Scenario: Serialize std::vector
- **WHEN** `serialize(std::vector<int>{1,2,3}, archive)` is called
- **THEN** the archive SHALL contain a JSON array with three integer elements

