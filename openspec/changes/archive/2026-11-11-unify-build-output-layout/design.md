## Context

See `proposal.md` — Why. The constraints that shape the approach:

- The runtime locator is `engine/Core/FileSystem/DataRoot.cpp`. It distinguishes an explicit override,
  an installed prefix and a compile-time fallback, and it resolves each category either beneath the
  root or through its own `ENGINE_DEV_*` macro.
- The data a configuration needs is not the source tree: built-in shaders are staged in the
  configuration's form (`glsl` for a native Debug build, baked SPIR-V/WGSL otherwise) by
  `cmake/stage_builtin_assets.cmake`, and physics modules are compiled per configuration.
- The build tree is used with two generator families: Visual Studio (multi-config) and Ninja
  (single-config). CMake appends a configuration directory automatically only for the former, so the
  layout has to name the configuration itself.
- The install prefix layout (`bin/` + `share/needrename/`) is the distribution contract and must stay
  as it is.
- Tests, examples and apps are all runtime targets; a developer runs them by starting the executable.

## Goals / Non-Goals

**Goals:**

- One directory per configuration that holds everything needed to run any executable of that
  configuration, found by proximity, with no environment setup, in the layout a distribution ships.
- Staged data that is provably in sync with the sources it is copied from, and a shader bake that
  runs only when a shader changed.
- One inventory of runtime artifacts: an install, the build tree and the Python development package
  cannot disagree about what a runtime contains.

**Non-Goals:**

- Changing the installed layout, the install components, the SDK package or the ZIP package.
- Making the build tree a distribution image; it carries test executables and debug information and is
  rebuilt by the build, which is what CPack exists to package.
- Removing `NEEDRENAME_DATA_ROOT`: it stays the explicit escape hatch, including for pointing a
  binary at the checkout to edit assets live.
- Changing the reflection pipeline, the shader bake itself, or the physics compilation.

## Decisions

### D1: One tree per configuration, and it is the install prefix

`output/<config>` holds `bin/` with the programs and `share/needrename` with the staged data root. The
linker and the staging commands write there directly: the build produces the shipped layout, and a
program runs as soon as it links.

The first iteration of this change kept the linker's output in `bin/<config>`, staged the data root
inside it, and assembled a second install-shaped tree in `output/<config>` from the install rules. That
gave four directories per configuration and copied every shipped byte twice - measured at ~437 MB of
binaries and ~148 MB of data across two configurations of two presets - for a property that `CPack` and
a real `cmake --install` already exercise. The duplication was rejected: one tree, one rule.

Consequence accepted: the tree is a development artifact, not a clean distribution image. It carries
test executables and debug information, and a renamed target can leave a file behind that the build no
longer produces. That is what CPack is for: it assembles a prefix from the install rules at packaging
time, which is where the "install only copies" contract is actually verified.

### D2: Proximity, with one step for both layouts

Because the build tree is a prefix, the locator needs exactly one executable-relative step:
`<executable dir>/../share/needrename`. It resolves a build-tree run and an installed run alike, so a
build tree can never need a second rule, a marker file, or an environment variable.

Alternative considered: making the run directory itself the data root, detected by the presence of
`builtin_assets`. It was rejected because detection by content adds a layout to specify and test,
whereas a fixed `share/needrename` name is the same name in the build tree and in a prefix.

### D3: Delete the compile-time developer fallback rather than repoint it

The fallback is removed, not repointed at a staged directory. It is the configuration-blind path
that caused the defect; a repointed version would still be a build-machine path baked into every
binary, would mask a failed staging run, and would make two rules where proximity already suffices.
`NEEDRENAME_DATA_ROOT` covers the workflows that want a specific directory.

Consequences: `engine/cmake_config.h.in` keeps only the five subdirectory-name constants, so the
header is machine-independent; `ENGINE_DEV_*`, `ENGINE_PHYSICS_SPIRV_DIR` and
`ENGINE_PHYSICS_SPIRV_CONFIG_SUBDIR` disappear; `ResolveDataDir` loses its per-category fallback
parameter and the physics loader matches.

### D4: One assembly function, for the prefixes the build tree is not

`needrename_stage_runtime(TARGET PREFIX COMPONENTS DEPENDS [EXTRA_COPY src dst])` removes the prefix,
runs `cmake --install --component` for each component, copies one extra directory if asked, and
touches a stamp. The Python development package is assembled with it, because its prefix differs from
the build tree: `needrename_sim/{share/needrename, bin/, *.pyd}` puts the package root where a prefix
has `bin/`. A distribution install is the same rules pointed at any prefix, and CPack is the same rules
pointed at its staging directory.

The alternative - a hand-written copy list per consumer - is what made the package and an install drift
apart before.

### D5: Staging is split by rate of change, and staging is authoritative

Four commands: built-in assets (copy + bake), sample assets, project templates, test fixtures; plus
physics modules from `engine/Physics`. Each removes its destination and copies, so a deleted source
cannot leave a stale copy, and each depends on the files it reads, so a no-op build runs nothing -
in particular no Tint. Built-in assets were previously copied together with the sample content and
project templates in the bake command, which made a sample-asset edit re-bake every shader.

Physics staging is unconditional: the runtime resolves `spirv/` through the data root for every
backend, so a Vulkan build without it would have no modules. Its per-configuration bake
subdirectory is flattened away here, which is why the runtime needs no configuration name.

### D6: Install reads staging, never the checkout

Every data `install()` sources the staged categories in the build tree
(`output/<config>/share/needrename/...`). That makes staging the single decision point for a category's
form and contents, keeps the "build preprocesses, install copies" split literal, and means an install
carries identical bytes to the tree it was built from.

### D7: Tests run the build tree

`anro_add_test` registers `output/<config>/bin/<test>` and sets `NEEDRENAME_DATA_ROOT=` (empty), so
every test exercises the proximity rule and the installed layout, and an inherited value cannot mask a
broken tree. Nothing has to be assembled first: the test binary is part of the default build and the
data root is staged by it.

The redundant `mrt_test_staged_builtin_assets` is deleted: with the build tree as the data root, its
explicit `NEEDRENAME_DATA_ROOT` run is what every other test now does.

### D8: The two GLSL-compiling tests name the source tree

`shader_refl_test` and `builtin_shader_compile_test` compile built-in GLSL sources, which a
baked-form data root deliberately does not carry. They receive
`NEEDRENAME_SOURCE_BUILTIN_ASSETS` and pass `<it>/shaders/include` to `ShaderCompiler`, which is the
one seam that adds a system include path. The engine's own includer is left alone: a runtime that
ships baked archives has nothing to compile.
