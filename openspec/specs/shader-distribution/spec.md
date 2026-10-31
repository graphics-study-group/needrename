# shader-distribution

## Purpose

Governs the form in which shader assets are shipped: built-in render/material shaders default to pre-baked SPIR-V embedded in their asset archives, GLSL sources remain a selectable developer mode, and physics compute SPIR-V ships inside the runtime data root.

## Requirements

### Requirement: Built-in shader assets ship as SPIR-V by default

Built-in shader assets (the `ShaderAsset` archives under the built-in asset database) SHALL ship with `storeType: SPIRV` and the compiled SPIR-V embedded in the archive, so loading requires no shader source files, no include tree, and no runtime compilation. Baking SHALL preserve each asset's GUID and its `builtin://` subpath so material libraries and other GUID references remain valid.

#### Scenario: Installed distribution contains no built-in GLSL

- **WHEN** a default (Release) install is inspected
- **THEN** the installed `builtin_assets/shaders/` contains SPIR-V-storing shader archives
- **AND** no built-in `*.glsl` shader sources or shader include tree are shipped

#### Scenario: GUID references survive baking

- **WHEN** a baked shader asset archive is loaded by GUID (e.g. the bloom compute shader referenced by a render graph)
- **THEN** it resolves to the same asset as before baking
- **AND** its SPIR-V binary loads without invoking the shader compiler

### Requirement: Shader mode switch

The build SHALL expose `ENGINE_SHADER_MODE` with values `auto`, `glsl`, and `spv`:

- `spv` — bake built-in shader assets to SPIR-V form
- `glsl` — ship built-in shader assets in GLSL form (sources plus include tree), compiled at runtime as today
- `auto` — `glsl` for Debug builds, `spv` otherwise

The chosen mode SHALL determine what is built and installed; the runtime loader behavior for each stored form is unchanged.

#### Scenario: Explicit spv mode

- **WHEN** `ENGINE_SHADER_MODE=spv`
- **THEN** the build produces SPIR-V-storing built-in shader archives regardless of configuration

#### Scenario: Auto selects per configuration

- **WHEN** `ENGINE_SHADER_MODE=auto` and a Debug build is made
- **THEN** built-in shader assets remain GLSL-stored and are compiled at runtime
- **WHEN** `ENGINE_SHADER_MODE=auto` and a Release build is made
- **THEN** built-in shader assets are baked to SPIR-V form

### Requirement: Baking matches runtime compilation semantics

Shader baking SHALL use the same GLSL compilation semantics as the engine's runtime shader compiler, including the built-in shader include search paths, so a baked binary is equivalent to what runtime compilation would produce for the same source and configuration.

#### Scenario: Include resolution parity

- **WHEN** a built-in shader using the shared include tree is baked
- **THEN** its includes resolve from the same include root the runtime compiler uses
- **AND** the baked SPIR-V loads and renders identically to the runtime-compiled form

### Requirement: User shaders keep runtime GLSL compilation

The engine SHALL continue to support loading user-provided shader source (`.glsl`) and compiling it at runtime, in every shader mode. `ENGINE_SHADER_MODE` governs the built-in asset corpus only.

#### Scenario: User GLSL shader loads under spv mode

- **WHEN** an application loads a `.glsl` shader file at runtime while `ENGINE_SHADER_MODE=spv`
- **THEN** it is compiled at runtime and usable
- **AND** no built-in asset is affected

### Requirement: Physics SPIR-V ships in the data root

Physics compute SPIR-V artifacts SHALL be installed under the runtime data root (`share/needrename/spirv/`) preserving their source-relative layout beneath the physics shader directory. Installed artifacts SHALL not retain the per-configuration subdirectory split used by the build tree; a given installation carries one configuration's artifacts.

#### Scenario: Installed physics shaders present

- **WHEN** installation completes
- **THEN** `share/needrename/spirv/` contains the physics compute `.spv` files at source-relative paths
- **AND** no build-tree absolute path is needed to find them at runtime

### Requirement: No shader build outputs in the source tree

No shader build pipeline SHALL write generated artifacts into the source tree. The legacy top-level shader pipeline that wrote `.spv` files into `builtin_assets/shaders/` SHALL be removed or redirected; its outputs are not read by any runtime code path.

#### Scenario: Source tree stays clean

- **WHEN** a full build completes and the source tree is inspected
- **THEN** no generated `.spv` or baked asset file exists under tracked source directories
- **AND** no `.gitignore` rule is needed to hide shader build outputs in `builtin_assets/`
