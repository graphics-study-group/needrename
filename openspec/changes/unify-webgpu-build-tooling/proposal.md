# Proposal: Unify WebGPU and shader build tooling

## Why

The WebGPU build originally required a separate full Vulkan/MSVC build to produce `shader_baker`, while desktop dependency discovery supported two native implementations with custom path and runtime-DLL handling. This made the presets interdependent, obscured which artifacts belong to each build tree, and left the physics WGSL bake dependent on a manually invalidated stamp. Dawn provides official CMake packages and matching prebuilt Debug/Release archives, so native WebGPU discovery and the host shader toolchain can both be made simpler and deterministic.

## What Changes

- **BREAKING** Desktop WebGPU supports Dawn only. The build consumes Dawn's official `DawnConfig.cmake` and `dawn::webgpu_dawn` target; wgpu-native discovery, runtime-DLL handling, cache variables, and documentation are removed. Dawn's Windows DXC components are copied from `DXC_ROOT/bin/x64` beside executables and into installations without SDK or `PATH` searches; FXC loads directly from System32.
- Native Windows MSVC configures without an explicit Dawn path download pinned, SHA-256-verified Debug and Release packages into `build/_deps/dawn`. Manual `Dawn_DIR`, dual-config `DAWN_ROOT`, and the original single Release prefix remain supported. Dual-config builds select the correct imported library per configuration; only Release-only packages retain the Release-MSVC-runtime fallback.
- Tint discovery is required only for WebGPU shader baking. Desktop automatic provisioning uses its Release prefix; browser builds use a manual prefix or explicit converter override and never download desktop Dawn.
- `shader_baker` is decoupled from the aggregate `Engine` target into a native C++ shader-tooling library containing only GLSL compilation, SPIR-V reflection/rewriting, structured-buffer layout, and archive support. WebGPU runtime modules do not acquire glslang or SPIRV-Cross as runtime dependencies.
- Every native engine preset builds its own `shader_baker`; `webgpu-desktop` no longer reads `build/msvc`. A lightweight `host-tools` preset builds only native shader tools for the browser build, replacing the full Vulkan-engine prerequisite.
- Physics GLSL compilation moves from a separately discovered `glslangValidator` executable to the same C++ baker used for built-in shaders. Per-source SPIR-V, WGSL, and interface outputs replace the directory-wide WGSL stamp, with explicit dependencies on sources, includes, and tools.
- Browser asset preloads explicitly depend on the complete staged runtime-data tree. Automatic `ExternalProject` construction of host tools inside the Emscripten build is deferred; the supported browser flow uses the small `host-tools` preset first.
- Build documentation and presets describe a Dawn-only desktop flow, native self-contained builds, and the two-command browser host-tools flow using the standard `C:\build_tools` layout as a local-machine example rather than a hard-coded project path.

## Capabilities

### New Capabilities

- `webgpu-build-tooling`: Dawn-only desktop dependency discovery, native host shader-tool construction, per-preset build-tree isolation, and the browser host-tools contract.

### Modified Capabilities

- `physics-gpu-shaders`: physics shaders are compiled by the shared C++ baker instead of a separately located `glslangValidator`, and WebGPU side artifacts use per-source incremental dependencies rather than a directory stamp.

## Impact

- **Build system**: `CMakeLists.txt`, `CMakePresets.json`, `cmake/DawnPrebuilt.cmake`, `cmake/WebGPUDependencies.cmake`, Emscripten/tool dependency glue, `third_party/CMakeLists.txt`, `engine/Rhi/CMakeLists.txt`, `engine/Render/CMakeLists.txt`, `engine/Physics/CMakeLists.txt`, and shader staging rules.
- **Tools**: `tools/CMakeLists.txt` and `tools/shader_baker/**`; a native shader-tooling target and a `host-tools` build shape are introduced without a Python rewrite.
- **Engine boundaries**: GLSL availability is described by a target capability rather than `ENGINE_RHI_BACKEND_VULKAN`; static host-tool compilation no longer inherits DLL import/export annotations. WebGPU runtime reflection remains unavailable and consumes baked interface descriptions.
- **Dependencies**: Desktop Windows Dawn/Tint can be automatically provisioned from pinned official archives; manual packages remain supported. DXC remains manually installed at `DXC_ROOT`. wgpu-native and `WEBGPU_ROOT` support are removed, and WebGPU builds no longer require the Vulkan SDK once physics compilation uses the baker.
- **Documentation**: desktop/browser WebGPU instructions, the native dependency guide, README build links, and stale handover-facing build directions are reconciled with the new flow.
- **Verification**: clean builds must prove that `webgpu-desktop` works without `build/msvc`, browser builds work with only `build/host-tools`, automatic and manual Dawn layouts select the intended libraries, incremental shader edits rebuild exactly the affected artifacts, and existing Vulkan/WebGPU rendering and physics checks still pass.
