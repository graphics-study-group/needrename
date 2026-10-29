## 1. Build options and hygiene

- [x] 1.1 Raise `cmake_minimum_required` to 3.21 and verify `cmake --preset msvc` and `cmake --preset linux-debug` still configure and build cleanly
- [x] 1.2 Add cache options `ENGINE_BUILD_TESTS`, `ENGINE_BUILD_EXAMPLES`, `ENGINE_BUILD_APPS`, `ENGINE_BUILD_TOOLS`, `ENGINE_BUILD_DOCS` (default ON) and `ENGINE_INSTALL_TESTS` (default OFF), gate the corresponding `add_subdirectory`/target creation, and verify: a default configure builds the same target set as before; `-DENGINE_BUILD_TESTS=OFF` yields no test targets and an empty `ctest` listing
- [x] 1.3 Gate the vendored reflection library's test subproject behind the test policy and verify `ENGINE_BUILD_TESTS=OFF` configures no `third_party/` test targets

## 2. Runtime data locator

- [x] 2.1 Implement the data-root locator (resolution order: `NEEDRENAME_DATA_ROOT` env var -> `SDL_GetBasePath()` + `../share/needrename` -> compile-time developer fallback) with a clear diagnostic naming the resolved path when the root is missing, and verify with a unit test covering the resolution order and the failure case
- [x] 2.2 Rewrite `engine/cmake_config.h.in` to layout constants + developer fallback only, generate it into the build tree instead of the source tree, and verify no `cmake_config.h` exists under source directories after a build while the existing test suite still passes
- [x] 2.3 Migrate engine internals (shader include root, physics SPIR-V root) from the `ENGINE_*_DIR` macros to the locator and verify shader-related and physics tests pass from the build tree
- [x] 2.4 Migrate examples, apps, and tests (including test fixture lookup) to the locator and verify the full `ctest` suite passes in the build tree on both platforms
- [x] 2.5 Redirect example runtime writes (project-template copies) to a writable scratch location and verify an example run leaves `git status` clean (no source-tree modifications)

## 3. Shader distribution

- [x] 3.1 Implement `tools/shader_baker` (linking the engine's shader compiler and includer) that rewrites built-in `.asset` archives from GLSL to embedded-SPIR-V form preserving each GUID, and verify baked archives load and render correctly in a windowed test
- [x] 3.2 Add `ENGINE_SHADER_MODE=auto|glsl|spv` with build-tree staging of baked assets, and verify the mode matrix: `auto` bakes in Release and keeps GLSL in Debug; explicit `spv`/`glsl` override the configuration
- [x] 3.3 Remove the legacy top-level shader pipeline and the `builtin_assets` `.spv` gitignore residue, and verify a full build leaves no generated shader artifacts in the source tree
- [x] 3.4 Wire shader content to the install per mode (spv: baked archives only; glsl: sources plus include tree) and verify the installed `builtin_assets/shaders/` content matches the selected mode

## 4. Physics SPIR-V relocation

- [x] 4.1 Add an install rule copying the active configuration's physics SPIR-V to `share/needrename/spirv/` at source-relative paths (no config subdirectory) and verify the installed layout
- [x] 4.2 Point the central physics shader loader at the locator's physics root (keeping the macro as developer fallback) and verify physics tests pass in the build tree and the installed physics app finds its shaders at runtime

## 5. Install layout and components

- [x] 5.1 Add `install(TARGETS ...)` for the engine modules, reflection library, editor library, imgui and ktx plus runtime dependency shipping (SDL3 runtime, engine DLLs) into `bin/`, and verify an installed example launches from an arbitrary working directory on Windows
- [x] 5.2 Add data install rules for the `runtime`, `apps-data`, `examples-data`, and `test-data` components, exclude `assets/img/`, and verify a runtime-only install runs engine binaries with no `img` content anywhere in the prefix
- [ ] 5.3 Set installed-library RPATH on Linux (`$ORIGIN`-relative) and verify an installed executable runs without `LD_LIBRARY_PATH`
- [x] 5.4 With `ENGINE_INSTALL_TESTS=ON`, install test executables and `share/needrename/test_data/` and verify an installed test runs; with it OFF, verify no test executable or test data is installed
- [x] 5.5 Add optional CPack ZIP packaging over the components and verify the `package` target produces `needrename-<version>-<platform>.zip` unpacking to the install layout

## 6. External consumption SDK

- [ ] 6.1 Define the export set and `needrename-config.cmake` (namespace `needrename::`, `find_dependency(SDL3)`/`find_dependency(Vulkan)`, vendored public headers and transitive link artifacts installed), and verify a scratch external project can `find_package(needrename CONFIG REQUIRED)` and build a minimal `needrename::Engine` executable
- [ ] 6.2 Install the reflection parser scripts/templates and per-module type lists under `share/needrename/reflection/`, implement `needrename_add_reflection()` with explicit type-list paths, and verify an external custom component using the reflection macros builds and serializes to the engine archive format (mirroring `editor_run_game_example`)
- [ ] 6.3 Mark the rename points (project name, package/config name, namespace prefix) with `TODO: rename` comments and verify they are all discoverable by a single text search
- [ ] 6.4 Verify the external scratch project builds with the engine source tree unavailable (e.g. renamed directory), proving the SDK is self-contained

## 7. Docs and end-to-end validation

- [ ] 7.1 Extend `docs/build_instructions/*` with install steps, the install layout, `NEEDRENAME_DATA_ROOT`, `ENGINE_SHADER_MODE`, and SDK usage, and verify every documented command works as written
- [ ] 7.2 Run the end-to-end acceptance on both platforms: default build + `ctest` green in the build tree, install, and installed examples/apps run from an unrelated working directory; verify `openspec validate installable-distribution` passes
