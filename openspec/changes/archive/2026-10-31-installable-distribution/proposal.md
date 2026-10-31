# Installable Distribution

## Why

The engine currently exists only as a build tree: there is not a single `install()` rule in the project, every runtime data location is a machine-absolute path baked into `engine/cmake_config.h` (generated into the **source** tree), builtin shaders are compiled from GLSL at runtime, and external projects can only consume the engine by living inside this repository. We want `cmake --install` to produce a self-contained distribution whose installed executables run out of the box, and a CMake package external projects can link against the way the in-tree examples do.

## What Changes

- **CMake feature options** gate what gets built: `ENGINE_BUILD_TESTS`, `ENGINE_BUILD_EXAMPLES`, `ENGINE_BUILD_APPS`, `ENGINE_BUILD_TOOLS`, `ENGINE_INSTALL_TESTS`, `ENGINE_BUILD_DOCS`; the unconditional `enable_testing()`/tests of `third_party/AnnoRefl` become gated; `cmake_minimum_required` is raised (3.21+) for runtime-dependency install support.
- **BREAKING** — Runtime data locations stop being absolute paths in a source-tree `cmake_config.h`. A runtime data locator resolves a data root: explicit override (environment variable) -> executable-relative installed layout (`SDL_GetBasePath()`) -> compile-time dev fallback. Engine internals (shader includes, physics SPIR-V) and all examples/tests/apps consume it. The generated config header moves out of the source tree.
- **Builtin shaders are shipped as SPIR-V.** A build-time `shader_baker` tool compiles each builtin GLSL `ShaderAsset` and rewrites its archive with `storeType: SPIRV` (embedded binary, GUID preserved — the archive format and loader already support this). `ENGINE_SHADER_MODE=auto|glsl|spv` selects the shipped form (`auto`: Debug=glsl, Release=spv). Runtime GLSL compilation stays available for user-provided shaders. The dead top-level `shader/` pipeline (writes `.0.spv` into the source tree, nothing reads it) is removed or repurposed as the baker's staging.
- **Physics compute SPIR-V** keeps its build-time glslang pipeline but is resolved at runtime through the data root (`share/needrename/spirv`) instead of an absolute build-tree macro; installed and dev resolution share one lookup.
- **Install rules** produce a portable layout: `bin/` (executables + all DLLs incl. SDL3/ktx/AnnoRefl/imgui) and `share/needrename/` (data root: `builtin_assets/`, `projects/` templates, `spv/`, `reflection/` pkl, optional `test_data/`). Data ships as install **components**: `runtime` (required), `apps-data`, `examples-data`, `test-data`, `sdk`. `assets/img/` (README screenshots) is never installed; `assets/` test content installs only with `test-data`. Installed executables must run with no external path configuration (Windows DLL adjacency; Linux `$ORIGIN`-based RPATH). Optional CPack ZIP packaging over the components.
- **External project consumption**: an installed CMake package `needrename` exports `needrename::Engine` (and module targets), engine + required vendored third-party headers, `find_dependency(SDL3)`/`find_dependency(Vulkan)`, and `needrename_add_reflection()` wiring the shipped reflection parser (scripts/templates + engine `reflection_data.pkl` type lists) so external components can use `REFL_SER_CLASS` exactly like the in-tree examples. Package name `needrename` is a deliberate placeholder; rename points are documented for a later one-shot rename.
- Examples/tests stop writing into the source tree at runtime (project-template copies go to a writable working directory instead of `example/`).

## Capabilities

### New Capabilities

- `build-feature-options`: CMake option matrix gating tests/examples/apps/tools/docs and third-party test subprojects; defaults keep the current dev build complete.
- `install-layout`: portable install prefix layout, install components, DLL/runtime shipping, "installed executables run" contract, optional CPack packaging.
- `runtime-data-resolution`: data-root resolution chain (override -> executable-relative -> dev fallback), config-header replacement for the absolute-path macros, writable-scratch rule for runtime writes.
- `shader-distribution`: builtin shader assets shipped as SPIR-V-embedded archives (GUID-preserving bake), `ENGINE_SHADER_MODE`, physics SPIR-V in the data root, removal of the dead source-tree-spilling shader pipeline.
- `external-project-consumption`: `find_package(needrename)` package, exported targets and headers, dependency discovery, `needrename_add_reflection()` with shipped parser + pkl type lists.

### Modified Capabilities

- `physics-gpu-shaders`: the "Runtime SPIR-V root exposed via cmake_config.h" requirement changes from an absolute build-tree macro to data-root-relative resolution (build pipeline and source layout unchanged); installed copies of the artifacts become part of the runtime data.

## Impact

- **Build system**: top-level and per-module `CMakeLists.txt` (options, install rules, export set), `third_party/AnnoRefl` (gated tests, exported parser), `CMakePresets.json` (bump, new cache variables), `cmake_minimum_required` bump.
- **Engine code**: `engine/cmake_config.h.in` is replaced/augmented by a runtime data locator; consumers of `ENGINE_*_DIR` macros (`ShaderIncluder`, `PhysicsSpirvLoader`, examples, tests, apps) migrate to it. `ShaderAsset` gains no loader changes (SPIR-V archive path already exists); a new `tools/shader_baker` executable links the existing `ShaderCompiler`.
- **Data**: `builtin_assets/shaders/*.asset` are baked at build time; physics `.spv` install from the build tree; `projects/` templates install; `assets/` splits into optional components; `assets/img/` excluded.
- **Consumers**: external projects get `find_package(needrename)` + `needrename_add_reflection()`; the reflection pkl files ship as SDK dev data (format migration to JSON is explicitly out of scope).
- **Docs**: `docs/build_instructions/*` gain install/SDK sections.
- **Out of scope**: pkl -> JSON migration of the reflection cache, NSIS/WIX installers, macro prefix rename (`ENGINE_*`), engine renaming beyond the `needrename` placeholder package name.
