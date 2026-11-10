## Why

A freshly built executable could not run from its build tree. It resolved its runtime data through
`ENGINE_DEV_DATA_ROOT_DIR`, a compile-time path into the checkout, while the data a configuration
actually needs - baked built-in shaders and per-configuration physics SPIR-V - was staged in a
sibling `stage/<config>` directory. That fallback was configuration-blind: a WebGPU build (and a
Vulkan build in spv mode) therefore loaded GLSL sources it has no compiler for, and the only
workaround was setting `NEEDRENAME_DATA_ROOT` by hand - which is why the desktop WebGPU suite could
not render anything.

Staging had the mirror-image problem. One command copied the built-in assets, baked the shaders and
also copied the sample content and project templates, but only declared the built-in asset tree as a
dependency. Editing a sample asset therefore did not restage, deleting one never removed its copy,
and any change re-ran the shader bake. Physics SPIR-V was staged for WebGPU only.

## What Changes

- **A staged data root per configuration, inside the run directory.** `bin/<config>` holds the
  executables and, at `share/needrename`, `builtin_assets/` (in the configuration's shader form),
  `assets/`, `projects/`, `spirv/` and `test_data/`, each produced by its own command with its own
  stamp and its own source-file dependencies. Physics SPIR-V is staged for every backend, not only
  WebGPU. A binary the linker just wrote therefore runs without setup.
- **An assembled product per configuration.** `output/<config>/` is `bin/` plus
  `share/needrename/` - the install layout, assembled from the install rules by
  `cmake --install --component ...`. Keeping the product separate from `bin/<config>/` is what makes
  the development product an install of the real rules rather than a hand-copied lookalike.
- **One assembly function.** `needrename_stage_runtime()` assembles a prefix from install
  components; the development product and the Python development package are both calls to it, and a
  distribution install is the same rules pointed at another prefix.
- **No compile-time data path.** `ENGINE_DEV_*`, `ENGINE_PHYSICS_SPIRV_DIR` and the per-category
  fallbacks are deleted; the generated configuration header carries only layout constants. A run
  resolves `NEEDRENAME_DATA_ROOT`, else `<executable dir>/../share/needrename`, else
  `<executable dir>/share/needrename`, else fails. Both executable-relative steps are anchored on the
  binary, so a build tree and an install each resolve their own configuration without an environment
  variable, and `NEEDRENAME_DATA_ROOT` remains the explicit override.
- **Tests consume the product.** `ctest` runs `output/<config>/bin/<test>` with
  `NEEDRENAME_DATA_ROOT` cleared, so every test proves the proximity rule works. The redundant
  staged-assets duplicate test is removed, the archive-form test validates the product, and the two
  Vulkan tests that compile GLSL receive the source asset directory explicitly.
- **Scratch leaves the tree.** The examples materialise their working project in the platform
  temporary directory instead of the working directory, which may be the product directory.

## Capabilities

### New Capabilities

- `build-output-layout`: the contract for the build tree's directories - the run directory with the
  data root staged inside it, and the assembled install-shaped product - what each contains, and that
  each is the runtime data root of a run started from it.

### Modified Capabilities

- `runtime-data-resolution`: the resolution chain loses the compile-time developer fallback and the
  per-category fallbacks, and gains the executable-relative build-tree step; a build-tree run and an
  installed one each resolve by proximity.

## Impact

- **Build system**: `CMakeLists.txt`, `cmake/RuntimeData.cmake` (rewritten), new
  `cmake/RuntimeAssembly.cmake` and `cmake/stage_tree.cmake`, `cmake/stage_builtin_assets.cmake`,
  `engine/Physics/CMakeLists.txt`, `cmake/install_rules.cmake`, `app/python/CMakeLists.txt`,
  `test/CMakeLists.txt`, `test/app/physics/CMakeLists.txt`, `test/engine/windowed/CMakeLists.txt`,
  `test/unit/shader/CMakeLists.txt`, `example/CMakeLists.txt`.
- **Runtime**: `engine/Core/FileSystem/DataRoot.{h,cpp}`, `engine/cmake_config.h.in`,
  `engine/Physics/PhysicsSpirvLoader.{h,cpp}`, `engine/Render/Asset/Shader/ShaderIncluder.cpp`.
- **Tests and examples**: `test/unit/core/data_root_test.cpp` (rewritten read-only),
  `test/unit/shader/shader_archive_form_test.cpp`, `test/unit/shader/shader_refl_test.cpp`,
  `test/unit/shader/builtin_shader_compile_test.cpp`,
  `test/unit/rhi/epoch_tracker_test.cpp` (configuration-independent include),
  `example/external_resource_loading_example/main.cpp`, `example/editor_run_game_example/main.cpp`.
- **Docs**: `README.md`, `docs/build_instructions/{windows_msvc,linux,desktop_webgpu,distribution}.md`,
  `app/python/README.md`.
