## Purpose

Defines a Dawn-only desktop WebGPU build and an isolated native shader-tool workflow for each build preset, including the small host-tools prerequisite used by browser cross-builds.

## ADDED Requirements

### Requirement: Desktop WebGPU uses only the Dawn CMake package

A desktop WebGPU configure SHALL consume Dawn's official CMake package and exported WebGPU target. Explicit `Dawn_DIR` and `DAWN_ROOT` settings SHALL take precedence over provisioning. `DAWN_ROOT` SHALL accept a single Release prefix or matching `debug/` and `release/` prefixes. On native Windows MSVC, a configure with neither setting SHALL download pinned, SHA-256-verified official Debug and Release archives beneath `build/_deps/dawn`. It SHALL NOT build Dawn from source, use a latest-release alias, write into a user-supplied prefix, or provide a wgpu-native path.

#### Scenario: Configure desktop WebGPU with Dawn installed

- **WHEN** a developer configures `webgpu-desktop` with Dawn discoverable through the documented prefix or package-directory setting
- **THEN** configuration SHALL resolve Dawn's exported WebGPU target without probing for wgpu-native
- **AND** no wgpu-native DLL copy or `WEBGPU_ROOT` value SHALL be required

#### Scenario: Configure desktop WebGPU without a Dawn path

- **WHEN** a native Windows MSVC configure has neither `Dawn_DIR` nor `DAWN_ROOT`
- **THEN** configuration SHALL reuse the complete pinned packages or download and verify them before compilation
- **AND** Debug SHALL select the Debug Dawn library while Release, RelWithDebInfo, and MinSizeRel SHALL select the Release library

#### Scenario: An explicit Dawn path is invalid

- **WHEN** an explicitly configured Dawn prefix or package directory is invalid
- **THEN** configuration SHALL fail with a diagnostic identifying the invalid setting
- **AND** it SHALL NOT silently substitute another Dawn version

#### Scenario: Browser configure does not resolve desktop Dawn

- **WHEN** the Emscripten preset configures
- **THEN** it SHALL use the `emdawnwebgpu` port for the target WebGPU implementation
- **AND** it SHALL NOT download or require the desktop Dawn WebGPU library or a wgpu-native installation

### Requirement: Release-only Dawn has an isolated MSVC compatibility path

A dual-config Dawn package SHALL use the normal per-configuration MSVC runtime. The build MAY force the Release runtime and undefine `_DEBUG` only when the selected package provides no Debug Dawn library. That compatibility traversal SHALL NOT affect dual-config builds.

#### Scenario: Debug uses a dual-config package

- **WHEN** a Debug target builds with matching Debug and Release Dawn prefixes
- **THEN** it SHALL use the Debug MSVC runtime and link the Debug Dawn library
- **AND** the Release-only `/U_DEBUG` compatibility traversal SHALL NOT run

### Requirement: Desktop Dawn compiler components are staged deterministically

On Windows, a desktop WebGPU build SHALL read exactly `dxcompiler.dll` and `dxil.dll` from `DXC_ROOT/bin/x64`, using the official DirectXShaderCompiler release layout. It SHALL NOT search SDK installations or `PATH` for these files. Every executable consuming `EngineDepWebgpu` SHALL have a transitive dependency that copies both files beside the executable for the active configuration, and installation SHALL place the same files in `bin`. The build SHALL NOT copy `d3dcompiler_47.dll`; Dawn instance creation SHALL add the Windows system directory to its runtime search paths so FXC loads from System32.

#### Scenario: Runtime components are present

- **WHEN** a developer builds any desktop WebGPU consumer with both files in the configured DXC release
- **THEN** the build SHALL copy the DXC components into that configuration's executable directory without a manual copy step
- **AND** the copy SHALL use only `DXC_ROOT/bin/x64`
- **AND** runtime initialization SHALL load FXC from the Windows system directory

#### Scenario: Runtime components are missing

- **WHEN** either DXC DLL is absent from the configured release
- **THEN** configuration SHALL fail with a diagnostic naming the missing files and `DXC_ROOT`

### Requirement: Tint is a WebGPU-only host dependency

WebGPU shader baking SHALL locate Tint from the resolved Release Dawn prefix and SHALL fail at configure time when no converter is available. Vulkan-only configures SHALL NOT search for or require Tint. A browser build SHALL use a native host Tint executable from a manual prefix or explicit override, never a wasm target tool or an automatic desktop-Dawn download.

#### Scenario: Tint is discovered from the Dawn prefix

- **WHEN** a WebGPU build configures with automatic desktop provisioning or `DAWN_ROOT` naming a documented layout
- **THEN** the configured converter SHALL be the `bin/tint` executable beneath the resolved Release prefix
- **AND** no converter path needs to be repeated on later build commands

#### Scenario: Tint is missing

- **WHEN** a WebGPU build cannot locate a usable Tint executable
- **THEN** configuration SHALL fail with an actionable message naming `DAWN_ROOT` and the explicit converter-path override

### Requirement: Native engine presets own their shader baker

Each native engine preset that needs shader artifacts SHALL build its native C++ `shader_baker` as part of its own build graph. A native WebGPU build SHALL NOT read a baker or other build product from `build/msvc`, `build/host-tools`, or another engine preset. Shader compilation and reflection dependencies used by the baker SHALL NOT become WebGPU runtime-library dependencies.

#### Scenario: Clean desktop WebGPU build without an MSVC engine tree

- **WHEN** a developer configures and builds `webgpu-desktop` on a machine with the documented compiler, Dawn, Tint, SDL3, and Python dependencies but no `build/msvc` tree
- **THEN** the build SHALL produce and use `build/webgpu`'s own native `shader_baker`
- **AND** all staged SPIR-V, WGSL, and interface artifacts SHALL be produced successfully

#### Scenario: WebGPU runtime dependency inspection

- **WHEN** the WebGPU engine runtime targets and their link interfaces are inspected after a successful build
- **THEN** they SHALL NOT acquire glslang or SPIRV-Cross merely because the native baker is present
- **AND** runtime WebGPU shader loading SHALL continue to consume baked interface descriptions rather than SPIR-V reflection

### Requirement: Browser builds use a lightweight host-tools preset

The project SHALL provide a `host-tools` preset that builds only the native tools required by a cross-build, without building the engine, apps, examples, tests, or documentation. The Emscripten build SHALL use that host-tools output and SHALL NOT require a full Vulkan engine build. A later automatic host-tool subbuild MAY be added without changing this contract.

#### Scenario: Build browser artifacts with only host-tools available

- **WHEN** a developer builds `host-tools` and then configures and builds `emscripten` without `build/msvc` or `build/webgpu`
- **THEN** the browser build SHALL use the native baker from `build/host-tools`
- **AND** all browser shader and asset artifacts SHALL build successfully

#### Scenario: Host tools are absent

- **WHEN** the Emscripten build is configured without its expected host baker and without an explicit host-baker override
- **THEN** configuration SHALL fail with a diagnostic naming the `host-tools` preset as the prerequisite

### Requirement: Runtime data is complete before browser page linking

Every Emscripten executable that preloads built-in assets or physics shader artifacts SHALL have an explicit target dependency on the corresponding complete staged runtime-data tree. A clean parallel build SHALL NOT rely on `ALL` target scheduling to make preloaded files available.

#### Scenario: Clean parallel browser build

- **WHEN** a developer builds the browser examples from an empty build tree with parallel compilation
- **THEN** all preloaded built-in assets and physics artifacts SHALL exist before each page links
- **AND** no manual staging target invocation SHALL be required

### Requirement: Build documentation matches the supported dependency model

The build instructions SHALL describe only Dawn-based desktop WebGPU, pinned automatic and optional manual Dawn/Tint provisioning, desktop compiler-component installation, self-contained native engine presets, and the browser `host-tools` prerequisite. Obsolete wgpu-native and full-MSVC-baker instructions SHALL be removed from maintained build documentation.

#### Scenario: A developer follows the documented WebGPU setup

- **WHEN** a developer follows the desktop or browser WebGPU guide from a clean checkout
- **THEN** the guide SHALL provide the automatic or manual Dawn/Tint setup and preset commands appropriate to that build
- **AND** it SHALL NOT instruct installing wgpu-native or building the full `msvc` engine to obtain `shader_baker`
