## 1. Staged data root

- [x] 1.1 Split staging into four commands - built-in assets (copy + bake), sample assets, project
      templates, test fixtures - each with its own stamp and its own `CONFIGURE_DEPENDS` source
      globs, aggregated by `runtime_data_stage`; verify editing a sample asset restages only
      `assets/` without running the shader bake, editing a built-in shader re-runs the bake, and a
      no-op rebuild runs no staging command
- [x] 1.2 Remove the sample-content and project-template copy from the bake command, which is what
      made an asset edit re-bake every shader
- [x] 1.3 Replace each category's destination instead of merging into it, so a deleted source file
      cannot leave a stale copy; verify by deleting an asset and rebooting the staging category
- [x] 1.4 Stage physics modules for every backend, not only WebGPU, and flatten the per-configuration
      bake subdirectory away; verify a Vulkan build carries `spirv/*.spv` and a WebGPU one carries
      `*.spv`, `*.wgsl` and `*.iface.json`
- [x] 1.5 Declare each category directory as an additional clean file on its staging target; verify
      `--target clean` followed by a rebuild leaves no stale category content
- [x] 1.6 Exclude documentation screenshots from the staged sample assets and the test fixtures'
      `CMakeLists.txt`/`__generated__` files
- [x] 1.7 Stage the data root at `output/<configuration>/share/needrename`, one level above the
      `bin/` the linker writes into; verify a program in `output/<configuration>/bin` starts from any
      working directory with no environment variable

## 2. One tree per configuration

- [x] 2.1 Write the configuration's executables, shared libraries and extension modules into
      `output/<configuration>/bin`, and keep import/static libraries in `lib/<configuration>`, on both
      generator families
- [x] 2.2 Delete `product_stage` and the assembled development product: with the build tree laid out
      as a prefix, a second install-shaped copy only duplicated bytes (measured at 437 MB of binaries
      and 148 MB of data across four configurations)
- [x] 2.3 Keep `needrename_stage_runtime(TARGET PREFIX COMPONENTS DEPENDS [EXTRA_COPY])` for the
      prefixes the build tree is not - the Python development package and a distribution install -
      so the package still cannot drift from the install rules
- [x] 2.4 Depend on *targets*, not `$<TARGET_FILE:...>`, for ordered assembly: the file form does not
      become project ordering under the Visual Studio generator, which assembled a stale package
- [x] 2.5 Point every data `install()` at the staged categories in the build tree instead of the
      checkout, so staging is the only place that decides a category's form
- [x] 2.6 Keep `LIBRARY DESTINATION` as the only destination on the Python extension: adding a
      `RUNTIME` destination moved `COMPONENT runtime` onto it and left the module in the
      `Unspecified` group, which silently stopped installing the extension

## 3. Resolution and consumers

- [x] 3.1 Delete `ENGINE_DEV_*`, `ENGINE_PHYSICS_SPIRV_DIR` and
      `ENGINE_PHYSICS_SPIRV_CONFIG_SUBDIR`, and the `ENGINE_DEV_DATA_ROOT_DIR="/"` definition the
      browser examples carried; the generated configuration header now holds only layout constants
- [x] 3.2 Drop the per-category fallback parameter from `ResolveDataDir` and the physics loader, so
      every category is root-relative in every layout
- [x] 3.3 Register each test as `output/<configuration>/bin/<test>` with `NEEDRENAME_DATA_ROOT=`
      cleared; verify the whole suite resolves its own configuration's data
- [x] 3.4 Delete `mrt_test_staged_builtin_assets` and repoint `skybox_golden_test` at the staged
      fixture
- [x] 3.5 Change `shader_archive_form_test` to take only the expected shader form and assert the
      staged shaders have it
- [x] 3.6 Give `shader_refl_test` and `builtin_shader_compile_test` the source built-in-asset
      directory explicitly and pass its include tree to `ShaderCompiler`
- [x] 3.7 Rewrite `data_root_test` as a read-only test: proximity resolution, every category beneath
      the root, the override winning, and a missing category throwing - with no directory created or
      deleted
- [x] 3.8 Move the examples' working project to the platform temporary directory
- [x] 3.9 Make the epoch-tracker test's Vulkan handle include configuration-independent, which the
      Release build needs
- [x] 3.10 Keep one executable-relative resolution step. An intermediate iteration added a second one
      for a build tree whose binaries sat beside `share/needrename`; when the tree became the prefix,
      that step and its second `data_root_test` registration were removed again

## 4. Verification

- [x] 4.1 Vulkan Debug suite green (87/87)
- [x] 4.2 Vulkan Release suite green (87/87)
- [x] 4.3 Desktop WebGPU Debug suite green (65/65)
- [x] 4.4 Python pytest green from the assembled development package; the headless-render example
      renders frames
- [x] 4.5 Emscripten pages build and the browser page runs
- [x] 4.6 Start a program from an unrelated working directory in every configuration and confirm it
      loads its own data with no environment variable
- [x] 4.7 Confirm the build tree holds one copy of each program and of the data root, and that a no-op
      rebuild stages nothing and assembles nothing
- [ ] 4.8 Linux suite (WSL2) - not run in this environment
