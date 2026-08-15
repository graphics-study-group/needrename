# reflection-module

## MODIFIED Requirements

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
