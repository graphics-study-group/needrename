## 1. Dawn-only dependency discovery

- [x] Replace desktop WebGPU discovery with `find_package(Threads REQUIRED)` and `find_package(Dawn CONFIG REQUIRED)` using `DAWN_ROOT`/`Dawn_DIR` hints, link `EngineDepWebgpu` to `dawn::webgpu_dawn`, and remove wgpu-native paths from `cmake/WebGPUDependencies.cmake` and the root build; verify a fresh `webgpu-desktop` configure resolves Dawn without `WEBGPU_ROOT`.
- [x] Scope required Tint discovery to WebGPU builds using `DAWN_ROOT/bin` and retain the explicit converter override; verify a Vulkan configure does not require Tint and a WebGPU configure without Dawn/Tint fails with the intended actionable diagnostic.
- [x] 1.3 Remove wgpu-native DLL-copy and library-guessing logic while preserving the Emscripten `emdawnwebgpu` interface; verify desktop Debug and Release targets link against Dawn's static WebGPU library without a wgpu-native DLL and browser configure never resolves the desktop Dawn library.
- [x] 1.4 Stage Dawn's Windows DXC DLLs from `DXC_ROOT/bin/x64` without SDK or `PATH` searches, load FXC from System32 through Dawn's explicit runtime search paths, and verify missing files fail configuration, single-target consumer builds copy the files, and install rules include them.
- [x] 1.5 Split WebGPU dependencies, host-baker resolution, and Dawn-specific MSVC runtime settings out of the root `CMakeLists.txt`; verify Dawn/Tint/DXC use only their documented explicit locations and browser configuration remains independent of desktop libraries.
- [x] 1.6 Add pinned, SHA-256-verified Dawn Debug/Release provisioning for native Windows MSVC, support explicit single and dual-config manual layouts, and restrict the Release-MSVC-runtime traversal to Release-only packages; verify automatic reuse, per-config library selection, and invalid explicit-path diagnostics.

## 2. Independent C++ shader tooling

- [x] 2.1 Introduce `ENGINE_SHADER_COMPILER_HAS_GLSLANG` and static-link guards for RHI/Render export macros, and remove unused baker archive includes; verify the Vulkan runtime still compiles its real shader compiler while WebGPU runtime keeps the unavailable implementation.
- [x] 2.2 Add the static `EngineShaderTooling` target with only the required compiler, reflection, structured-buffer, SPIR-V rewrite, WGSL/interface, JSON, and logging sources, then link `shader_baker` to it instead of `Engine`; verify `shader_baker` builds and runs its existing archive and `--spirv-dir` modes without engine DLLs.
- [x] 2.3 Move glslang/SPIRV-Cross third-party conditions to the native tooling requirement, remove `ENGINE_RHI_BAKE_REFLECTION`, and keep WebGPU runtime on the no-reflection implementation; verify WebGPU runtime link interfaces contain neither glslang nor SPIRV-Cross.

## 3. Preset-local tool construction

- [x] 3.1 Add the native tooling subtree before engine targets so every native engine preset builds its own `shader_baker`, use `$<TARGET_FILE:shader_baker>` in native shader commands, and remove fallback searches in other engine build trees; verify `cmake --build --preset webgpu-desktop --target shader_baker` succeeds without `build/msvc`.
- [x] Add `ENGINE_HOST_TOOLS_ONLY` and `host-tools` configure/build presets with engine, apps, examples, tests, docs, and shared libraries disabled; verify the build produces `build/host-tools/bin/Release/shader_baker.exe` and no engine runtime targets.
- [x] Point Emscripten host-baker resolution only at an explicit override or the deterministic host-tools output, with a configure-time error naming the prerequisite; verify missing host tools fail early and `build/emscripten` no longer references `build/msvc` or `build/webgpu` baker paths.

## 4. Unified physics shader pipeline

- [x] Add a single-source C++ baker mode that emits SPIR-V plus optional WGSL/interface outputs through the existing compile, reflect, rewrite, convert, and validation code; verify a valid compute source produces all requested outputs and an invalid source returns nonzero with its path in the diagnostic and no accepted output.
- [x] 4.2 Replace `glslangValidator` discovery and per-source validator commands in `engine/Physics/CMakeLists.txt` with baker commands, declare per-source outputs and dependencies on sources/includes/baker/Tint, and remove `.wgsl_stamp`; verify a clean WebGPU build produces every `.spv`, `.wgsl`, and interface file without invoking the system validator.
- [x] 4.3 Reconfigure after adding a shader and rebuild after editing one source and one shared include; verify new outputs appear without CMake list edits or stamp deletion and unrelated artifact mtimes remain unchanged.
- [x] 4.4 Consolidate built-in assets and physics outputs under `runtime_data_stage`, preserve engine/test dependencies, and add explicit dependencies from every Emscripten target using `--preload-file`; verify a clean parallel browser build stages all data before each page links without a manual staging command.

## 5. Documentation and stale-contract cleanup

- [x] 5.1 Document pinned automatic Dawn provisioning and optional manual single/dual layouts in `webgpu_dependencies.md`, update `desktop_webgpu.md`, `browser_webgpu.md`, `environment_variables.md`, and `README.md` for self-contained native builds and the `host-tools` browser prerequisite, and remove wgpu-native, `WEBGPU_ROOT`, DLL-copy, full-MSVC-baker, validator, and stamp instructions; verify maintained build docs contain only the new commands.
- [x] 5.2 Update CMake/source comments that describe wgpu-native support, Vulkan-only baker construction, WebGPU bake-time runtime reflection, or manual stamp invalidation; verify `git grep` finds no such stale statements outside historical OpenSpec/handover records.

## 6. End-to-end verification

- [x] 6.1 From a fresh build tree with no `build/msvc`, configure and build `webgpu-desktop` Debug, then run `webgpu_spike`, `webgpu_clear_example`, the staged-asset/MRT/skybox checks, and the full WebGPU CTest suite with the staged data root; verify all selected checks pass on Dawn and runtime targets have no shader-tool-only dependencies.
- [x] 6.2 Build and test the Vulkan `msvc-debug` preset and its own native baker; verify existing Vulkan rendering, physics, shader-archive, and reflection tests remain green without Tint or a separately invoked physics `glslangValidator`.
- [x] 6.3 Build only `host-tools`, then configure and build `emscripten` without the native engine build trees; serve the pages and run the CDP capture for the physics example, verifying the console's increasing numeric probe, completed frame run, and absence of shader/staging errors.
- [x] 6.4 Review the final diff and build outputs; verify no source assets were baked in place, no preset consumed another engine preset's baker or stage directory, no `.wgsl_stamp` remains, and `git diff --check` passes.
