# Design: Installable Distribution

## Context

See `proposal.md` for motivation. Constraints that shape the approach:

- All runtime data locations today are absolute paths in `engine/cmake_config.h`, generated **into the source tree**, consumed by engine internals (`ShaderIncluder`, `PhysicsSpirvLoader`) and by every example/test/app via `#include <cmake_config.h>`.
- Shader assets already support both storage forms: the `.asset` archive carries `storeType: GLSL | SPIRV`; the loader implements both, and the GUID lives inside the archive. The SPIR-V form is unused today — every built-in shader archive stores `GLSL` and compiles at load time through the vendored glslang (with a `shaders/include/` include root).
- Physics compute SPIR-V is compiled at build time (Vulkan SDK `glslangValidator`) into `${CMAKE_BINARY_DIR}/engine/Physics/spirv/<debug|release>/` and located through an absolute macro.
- The reflection parser (`third_party/AnnoRefl/parser`) runs Python+libclang at build time and consumes parent projects' `reflection_data.pkl` caches purely as a known-type-name set (the path values in the map are never read).
- In-tree examples already model the desired external consumption: `target_link_libraries(... Engine)` plus, for reflected components, `add_reflection_parser(... parent_projects meta_framework_generation meta_render_generation)`.
- The project has zero `install()` rules; shared libraries land together in `bin/` via output-directory conventions and a POST_BUILD copy of SDL3/ktx.

## Goals / Non-Goals

**Goals:**

- One install layout that works for Windows and Linux, single- and multi-config, with installed executables runnable from any working directory.
- One data-resolution mechanism shared by engine internals and consumer executables, with the developer build tree working exactly as today.
- Built-in shaders shipped as SPIR-V by default without losing the GLSL developer mode or user-shader runtime compilation.
- An external consumption story (`find_package(needrename)`) that mirrors the in-tree example experience, including reflection codegen.

**Non-Goals:**

- Migrating the reflection cache format from pickle to JSON (follow-up change to the reflection build system).
- Windows installer toolchains (NSIS/WIX); ZIP packaging is the only CPack target.
- Renaming the engine, the `ENGINE_*` macro family, or target names (`module-target-naming` stays authoritative). Only the package identity is introduced, as `needrename`.
- Hot-reload workflows for shipped shaders.

## Decisions

### D1. Portable layout with an executable-relative data root

Install produces `bin/` + `share/needrename/{builtin_assets,projects,spv,reflection,test_data}`. A runtime **data locator** resolves the data root by first match: `NEEDRENAME_DATA_ROOT` env var -> `SDL_GetBasePath()` + `../share/needrename` -> compile-time developer fallback (absolute paths, kept for the build tree).

- *Why:* the engine already depends on SDL3, so `SDL_GetBasePath()` adds no dependency; executable-relative resolution is the only scheme that survives moving/repackaging the install prefix. The env override gives test harnesses and unusual deployments an escape hatch.
- *Alternatives:* FHS-style absolute `CMAKE_INSTALL_FULL_DATADIR` baked at configure time (rejected: breaks relocatable zips and reinstalls); staging all data into the build tree so one code path serves both (rejected: 25+ MB copied per config and a slow build step for zero user value); two config headers for dev/installed (rejected: two code paths to keep in sync).

### D2. Config header becomes layout constants + dev fallback

`engine/cmake_config.h.in` stops being the runtime source of truth. It defines the data-root **subdirectory names**, the developer fallback absolute paths, and the physics per-config subdir selector. It is generated into the build tree (not the source tree). Callers migrate from `ENGINE_*_DIR` macros to the locator's accessors (`data_root()`, `builtin_assets_dir()`, `physics_spirv_dir()`, ...); the macros survive only inside the locator as fallback inputs.

- *Why:* removes machine-absolute paths from the installed SDK while keeping the zero-setup dev loop; one resolution code path to test.

### D3. Bake built-in shaders with the engine's own compiler, via a `shader_baker` tool

A `tools/shader_baker` executable links the engine's existing `ShaderCompiler`/`ShaderIncluder` (not the glslangValidator CLI) and rewrites each built-in `.asset` archive from `storeType: GLSL` to `storeType: SPIRV` with the binary embedded, preserving `Asset::m_guid`. Output goes to the build tree's data staging area (never the source tree) and is what gets installed.

- *Why:* the archive format and loader already support the SPIR-V form (verified in `ShaderAsset::save/load_asset_to_archive`), so no runtime change is needed; reusing the engine compiler guarantees include-resolution and flag parity with runtime compilation; baking through the normal archive save keeps GUID references (`builtin://shaders/bloom.comp.asset` etc.) valid.
- *Alternatives:* glslangValidator CLI per file (rejected: different include handling, would need to re-implement archive rewriting anyway); storing loose `.spv` files (rejected: loses GUID/asset identity, adds a parallel lookup path); editor-time import (rejected: baking must happen in CI/build, not interactively).

### D4. `ENGINE_SHADER_MODE=auto|glsl|spv`, default `auto`

`auto` bakes for non-Debug configurations and ships GLSL for Debug (preserving `-gVS` shader debugging and current Debug behavior). User `.glsl` loading + runtime compile stays functional in every mode — the switch governs the built-in corpus only.

### D5. Physics SPIR-V: keep the build pipeline, relocate the runtime lookup

The existing `physics_shader` target and per-config build-tree outputs stay (the `physics-gpu-shaders` build contract is unchanged); an install rule copies the active configuration's artifacts to `share/needrename/spirv/` (flattened — no config subdir). The runtime helper resolves through the same data locator; `ENGINE_PHYSICS_SPIRV_DIR` becomes the locator's developer fallback.

- *Why:* minimal delta to a heavily-specified pipeline; the only broken link is the runtime absolute path, which D2 already fixes.

### D6. Export surface: unchanged target names under a `needrename::` namespace

`install(TARGETS ... EXPORT needrenameTargets)` for the engine modules, the reflection library, and editor library; `needrename-config.cmake` calls `find_dependency(SDL3)` / `find_dependency(Vulkan)`. Vendored dependencies that the `Engine` interface currently passes to consumers (glm/json/vma headers, glslang libs, imgui/ktx) are installed as part of the SDK export set rather than refactoring the `EngineDep*` interfaces first.

- *Why:* `module-target-naming` fixes the target names; the namespace provides the package identity. Exporting the vendored deps as-is is the low-churn path; narrowing `EngineDepVulkan`'s glslang leak is real interface hygiene but is separable follow-up work.
- *Alternatives:* refactor `EngineDep*` before exporting (rejected for this change: touches every module's link graph); require consumers to `find_package(glslang)` (rejected: version skew against the vendored copy).

### D7. Reflection export ships parser + pkl as SDK data

`needrename_add_reflection()` wraps the existing `parser.cmake` logic; the parser scripts/templates install under the SDK (e.g. `share/needrename/reflection/parser/`) and the per-module `reflection_data.pkl` caches install next to them as parent type lists. The pkl paths are passed explicitly (not read from build-target properties, which don't exist for an installed SDK). The consumer's generated code goes into the consumer's own tree.

- *Why:* matches how the in-tree example already works (`parent_projects` wiring), so external components behave identically. Passing pkl paths explicitly is also exactly the seam the future pkl->JSON change needs — only the file format changes, not the install layout or function signature.

### D8. Executables never write into data or source trees

Examples that copy project templates into `example/` at runtime switch to a writable scratch location (current working directory, or `SDL_GetPrefPath` where appropriate). Installed templates and built-in assets are read-only by contract.

### D9. Components + optional CPack ZIP

Components: `runtime`, `apps-data`, `examples-data`, `test-data` (only with `ENGINE_INSTALL_TESTS=ON`), `sdk`. `assets/img/` is excluded outright. CPack is a two-line add-on (`CPACK_GENERATOR ZIP` + `include(CPack)`), kept optional; `cmake --install` remains the primary delivery.

## Risks / Trade-offs

- [SDK export set grows to vendored deps (glslang static libs etc.)] -> Accept for now; document the `EngineDepVulkan` interface leak as follow-up hygiene. Consumers get a working package instead of a refactor-first migration.
- [Debug GLSL mode keeps runtime-compile cost and ships sources] -> Intended: Debug prioritizes debuggability; `spv` can be forced per build.
- [Baked SPIR-V loses per-configuration debug info distinctions] -> Bake tool honors the configuration's debug-info flags; installed non-Debug builds carry release SPIR-V by design.
- [Multi-config MSVC installs mix artifacts if misused] -> Install is per `--config`; the flattened `spv/` mirrors the installed configuration; layout scenario covers this.
- [Reflection parser needs Python + libclang on external dev machines] -> Unavoidable while codegen is build-time; the parser already fails with actionable messages. Documented in SDK docs.
- [pkl pickle protocol may not load on older consumer Python] -> Accept for this change; the JSON follow-up removes the format entirely. Type names are the only data at risk and regeneration from headers is the manual fallback.
- [Examples' 2 MB sample asset (`red_brick_sphere_512.glb`) in examples-data] -> Accept; can shrink later without spec change.

## Migration Plan

1. Land options + locator + example/test migration first (dev workflow unchanged: defaults ON, dev fallback resolves as today).
2. Land baking + shader mode; verify Debug/Release matrix on both platforms.
3. Land install rules + components; validate "installed exe runs from any CWD" on Windows and Linux.
4. Land export package + reflection function; validate with a scratch external project in CI or manually against the docs.
5. Rollback: everything is additive behind `install()`; removing the install/export blocks restores the pre-change workflow. The only behavioral source change is the locator migration, which keeps dev behavior identical.

## Open Questions

- Whether `examples-data` should keep the 2 MB GLB sample or swap to a tiny generated asset (cosmetic; no spec impact).
- Future: slim `EngineDepVulkan` so glslang stops leaking into consumer link interfaces (tracked as follow-up, not blocking).
- Whether the JSON type-list migration (follow-up change) should also merge per-module lists into one file.
