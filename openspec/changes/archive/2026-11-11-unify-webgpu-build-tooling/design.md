## Context

The original root build located either wgpu-native or Dawn through `FindWebGPU.cmake`, discovered Tint globally, and found `shader_baker` from a previous Vulkan/MSVC build. The unified setup uses `WebGPUDependencies.cmake` instead. The baker linked the aggregate `Engine`, although it uses only the shader compiler, SPIR-V reflection/layout code, JSON archive handling, and converter invocation. Physics shaders used a separate `glslangValidator` path, followed by a directory-level WGSL stamp. See `proposal.md` for the motivation and the delta specs for the externally visible contracts.

The installed Dawn prefix already contains `DawnConfig.cmake`, a static `dawn::webgpu_dawn` target, and `bin/tint.exe`. Native presets use compilers that can execute their own output; Emscripten cannot, so its native tools remain a separate build concern.

## Goals / Non-Goals

**Goals:**

- Use one official desktop WebGPU dependency path and one native C++ shader toolchain.
- Keep every native engine preset independent of every other engine build tree.
- Restrict the browser exception to a minimal host-tools build with deterministic outputs.
- Preserve Vulkan runtime GLSL compilation while keeping glslang and SPIRV-Cross out of WebGPU runtime targets.
- Make shader and staging dependencies correct for clean parallel and incremental builds.

**Non-Goals:**

- No Python rewrite of the baker and no new Python shader dependencies.
- No package-manager provisioning or source build of Dawn/Tint; automatic provisioning is limited to pinned official prebuilt Windows archives.
- No wgpu-native compatibility layer or migration path.
- No `ExternalProject` host-tool subbuild in this change; the documented browser flow uses `host-tools` explicitly.
- No change to WGSL runtime binding, shader archive formats, RHI backend selection, or rendering behavior beyond validating the move from wgpu-native to Dawn.
- No broader CMake reorganization beyond the modules and conditions needed by this change.

## Decisions

### 1. Consume Dawn's official package and remove the desktop custom finder

For desktop WebGPU, call `find_package(Threads REQUIRED)` and `find_package(Dawn CONFIG REQUIRED)`, then link `EngineDepWebgpu` to `dawn::webgpu_dawn`. `Threads::Threads` is required because Dawn's exported target names it without finding it in `DawnConfig.cmake`.

Use `Dawn_DIR` or `DAWN_ROOT` for explicit package selection with `NO_DEFAULT_PATH`. `DAWN_ROOT` accepts either the original single Release prefix or matching `debug/` and `release/` prefixes. An explicitly invalid root is an error. When neither setting is present on native Windows MSVC, `DawnPrebuilt.cmake` downloads the pinned `v20261105.214757` Debug and Release archives, verifies their SHA-256 hashes, and extracts them beneath `build/_deps/dawn`. It never writes into a user-supplied prefix, probes `PATH`, uses a latest-release alias, or builds Dawn from source.

For a dual-config package, load the Release export for Dawn's transitive interface and add the same revision's Debug library as `IMPORTED_LOCATION_DEBUG`; map RelWithDebInfo and MinSizeRel to Release. This lets the multi-config generator select the correct library and MSVC runtime. Keep the existing Release-runtime traversal only for a Release-only package. Tint always comes from the Release prefix. Emscripten uses only the manual prefix's native Tint and never triggers the desktop download.

The Emscripten branch alone retains the small imported `WebGPU::WebGPU` interface carrying `--use-port=emdawnwebgpu`. Remove wgpu-native names, `WEBGPU_ROOT`/`WEBGPU_SDK` hints, static-versus-import-library guessing, and wgpu-native DLL-copy rules. Dawn's static target removes only the `webgpu_dawn` DLL requirement.

Dawn's Windows D3D backend dynamically loads DXC and FXC compiler components even when Dawn itself is static. Extract an official DirectXShaderCompiler release at `DXC_ROOT` and read exactly `bin/x64/dxcompiler.dll` and `bin/x64/dxil.dll`; fail at configure time if either is absent. A `dawn_dxc_stage` target copies those files with `copy_if_different` into the active configuration's unified executable directory. A transitive dependency from `EngineDepWebgpu` covers every consumer, including single-target builds, and install rules place the same files in `bin`. Do not copy `d3dcompiler_47.dll`: desktop runtime instance creation adds the directory returned by `GetSystemDirectoryA` to Dawn's `additionalRuntimeSearchPaths`, so FXC loads from System32.

Keep the root build file focused on project-wide options and target composition. `cmake/DawnPrebuilt.cmake` owns explicit-package validation, pinned downloads and imported configurations; `cmake/WebGPUDependencies.cmake` owns Tint/DXC and the backend interface; `cmake/ShaderTooling.cmake` owns host-baker resolution; `cmake/MsvcRuntime.cmake` owns only the Release-only fallback.

Alternative considered: retain wgpu-native behind another option. Rejected because it preserves two dependency contracts and the custom runtime packaging this change intends to eliminate.

### 2. Build a small static shader-tooling library instead of linking Engine

Introduce `EngineShaderTooling` as a native static library. Its initial source set is the shader compiler/includer, SPIR-V parameter reflection, structured-buffer/placer implementation, and the baker's SPIR-V rewrite and WGSL/interface code. Link only the required glslang, SPIRV-Cross, JSON, and logging support; do not link `Engine`, `EngineRender`, or `EngineRhi`.

Compile shared sources into the runtime and tooling targets separately rather than reusing an object library with incompatible compile definitions. Replace the backend-based GLSL guard with `ENGINE_SHADER_COMPILER_HAS_GLSLANG`, set by the Vulkan runtime and the tooling target independently. Add static-link guards for RHI/Render export macros so tooling objects do not carry DLL import/export annotations. Remove the baker's unused `AnnoRefl/Archive.h` include and avoid adding archive/runtime systems not exercised by the tool.

`EngineShaderTooling` is built whenever a native build needs physics shaders, independent of the optional miscellaneous-tools switch. WebGPU `EngineRhi` always uses the no-reflection implementation; remove `ENGINE_RHI_BAKE_REFLECTION` and its effect on WebGPU runtime sources and third-party dependencies.

Alternative considered: compile the real shader compiler back into WebGPU `EngineRender` and keep `shader_baker -> Engine`. Rejected because it makes a build-time compiler a WebGPU runtime dependency and leaves the browser with a different, less coherent tool boundary.

### 3. Give native presets and host-tools one tool definition

Move the baker and its tooling into a CMake subtree that can be added before the engine. Native engine presets build `shader_baker` directly in their own trees, and shader custom commands use `$<TARGET_FILE:shader_baker>` plus a target dependency.

Add an `ENGINE_HOST_TOOLS_ONLY` configure path and a `host-tools` preset. That path configures only the dependencies and native tool subtree, with shared libraries, engine, apps, examples, tests, and docs disabled. Its output is `build/host-tools/bin/Release/shader_baker.exe`.

For Emscripten, resolve `ENGINE_HOST_SHADER_BAKER` only from the explicit override or that deterministic host-tools path. Fail during configure with the host-tools command if neither exists. Remove all fallback searches under `build/msvc` and `build/webgpu`.

Alternative considered: nest host-tools with `ExternalProject` immediately. Deferred because a separate native generator and build-byproduct graph add complexity without changing the tool architecture; it can later wrap the same host-tools project.

### 4. Use the baker as the single GLSL compiler and emit per-source outputs

Add a single-source bake mode to the existing C++ executable. For each physics source it writes SPIR-V and, when requested for WebGPU, WGSL and the interface description using the same reflection, rewrite, sampler mapping, and validation code as built-in assets. Keep the existing batch archive mode for built-in shader assets.

Declare every physics source's `.spv`, `.wgsl`, and interface output explicitly in `add_custom_command`. The baker emits a Makefile-compatible depfile containing the include files actually read by that source; combine it with explicit dependencies on the source, baker target or host executable, and Tint for WGSL outputs. This rebuilds all consumers of an edited include without invalidating unrelated shaders. `physics_shader` aggregates these files, `physics_shader_stage` copies the resulting tree, and no `.wgsl_stamp` remains.

This removes the separate `glslangValidator` lookup and the WebGPU build's remaining Vulkan SDK requirement. Vulkan retains its SDK for runtime rendering; only the build-time validator dependency disappears.

### 5. Make runtime-data staging an explicit target contract

Create or consolidate a `runtime_data_stage` target covering built-in assets and the configuration's physics artifacts. Engine libraries and tests retain their existing dependencies, and every Emscripten target using `--preload-file` depends on `runtime_data_stage` before link. Keep generated artifacts in the active build tree; never bake into source assets or another preset's stage directory.

### 6. Migrate documentation and validation together with the build changes

Replace maintained wgpu-native instructions with Dawn installation/package discovery and update desktop/browser commands to the new preset boundaries. Historical handover evidence remains historical; current build guides must not refer to the old prerequisite.

Validation uses fresh build directories rather than relying on an existing baker or cached package path. It includes native clean builds, host-tools-only browser builds, incremental source/include edits, Dawn-based desktop rendering and physics checks, the existing Vulkan regression suite, and browser console/probe verification.

## Risks / Trade-offs

- **Dawn exposes different validation or runtime behavior from wgpu-native** → Treat the dependency switch separately in test evidence; run the skybox golden, MRT, compute, physics, editor, and browser checks and fix only regressions caused by the supported Dawn path.
- **The tooling source subset still carries hidden runtime or export dependencies** → Resolve them at the tooling boundary with capability/static macros and small source lists; do not fall back to linking the aggregate Engine.
- **Every native build now compiles glslang and SPIRV-Cross for its own tools** → Accept some additional clean-build time in exchange for preset isolation; keep these libraries out of WebGPU runtime link interfaces and use the host-tools-only path for cross-builds.
- **A per-source baker invocation may expose incomplete command-line or error handling** → Add focused compile/bake CLI checks and ensure failed commands do not leave outputs that satisfy later builds.
- **Host-tools remains a manual browser prerequisite** → Fail early with the exact preset command and document it; defer automatic nesting rather than hiding another cross-build dependency.
- **Prebuilt archives are large, unsigned nightly artifacts and may be unavailable or corrupt** → Pin revisioned URLs and SHA-256 hashes, reject incomplete extraction markers, preserve explicit manual packages, and validate both Debug and Release links. Accept the initial download size in exchange for zero manual Dawn setup.

## Migration Plan

1. Switch desktop dependency discovery to Dawn and establish a clean Dawn-based validation baseline.
2. Extract the tooling library, add static/capability guards, and make native presets build their own baker.
3. Add the host-tools-only configure path and point Emscripten to it; remove old cross-tree baker discovery.
4. Move physics compilation to per-source baker commands and make browser staging dependencies explicit.
5. Remove obsolete wgpu-native, stamp, validator, and documentation paths, then run the full clean and incremental validation matrix.

Rollback is build-system-local: restore the previous dependency and tool CMake paths and regenerate the affected build trees. No persistent asset or runtime-data migration is required; staged artifacts are disposable outputs.
