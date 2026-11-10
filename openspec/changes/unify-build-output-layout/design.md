## Context

See `proposal.md` — Why. The constraints that shape the approach:

- The runtime locator is `engine/Core/FileSystem/DataRoot.cpp`. It distinguishes an explicit override,
  an installed prefix and a compile-time fallback, and it resolves each category either beneath the
  root or through its own `ENGINE_DEV_*` macro.
- The data a configuration needs is not the source tree: built-in shaders are staged in the
  configuration's form (`glsl` for a native Debug build, baked SPIR-V/WGSL otherwise) by
  `cmake/stage_builtin_assets.cmake`, and physics modules are compiled per configuration.
- The build tree is used with two generator families: Visual Studio (multi-config) and Ninja
  (single-config). CMake appends a configuration directory automatically only for the former.
- The install prefix layout (`bin/` + `share/needrename/`) is the distribution contract and must stay
  as it is.
- Tests, examples and apps are all runtime targets; a developer runs them by starting the executable.

## Goals / Non-Goals

**Goals:**

- One directory per configuration that holds everything needed to run any executable of that
  configuration, found by proximity, with no environment setup.
- Staged data that is provably in sync with the sources it is copied from, and a shader bake that
  runs only when a shader changed.
- One inventory of runtime artifacts: an install, a development product and the Python development
  package cannot disagree about what a runtime contains.

**Non-Goals:**

- Changing the installed layout, the install components, the SDK package or the ZIP package.
- Making the development product a distribution format; it is a development artifact that carries
  test executables and is rebuilt by the build.
- Removing `NEEDRENAME_DATA_ROOT`: it stays the explicit escape hatch, including for pointing a
  binary at the checkout to edit assets live.
- Changing the reflection pipeline, the shader bake itself, or the physics compilation.

## Decisions

### D1: Two directories per configuration, not one

`stage/<config>` is the build's data root and `output/<config>` is the assembled product. They are
separate because they answer different questions: staging is what the build produced (one command per
category, each with its own stamp), and assembly is a copy of binaries plus staged data into a
prefix. Merging them was considered - the stash's earlier draft had a single flat `output/<config>`
holding the executables *and* the data, which removes a copy - and rejected here for two reasons.
First, the plan's contract is that `install` only copies and assembles, which needs a staging
directory that is not also the install destination. Second, and decisively, the product must be
install-shaped: `data_root_test` and the locator's proximity rule are then one rule for a build tree
and a prefix, instead of a build-only layout that has to be taught to the locator.

Consequence accepted: a configuration's data is copied twice (source -> stage, stage -> product).
Both copies are skipped when nothing changed, because each has its own stamp and its own dependency
list.

### D2: The product is install-shaped, so proximity is the only rule

`output/<config>` contains `bin/` and `share/needrename/`, exactly like a prefix. The locator's
existing second step - `<executable dir>/../share/needrename` - therefore resolves a build-tree run
with no new branch, no new `DataRootSource` value and no marker file.

Alternative considered: a flat product directory whose own directory is the data root, detected by
the presence of `builtin_assets`. It was rejected because it is a second layout to specify, test and
keep in step with the install, for the sole benefit of removing one `share/` level.

### D3: Delete the compile-time developer fallback rather than repoint it

The fallback is removed, not repointed at the product directory. It is the configuration-blind path
that caused the defect; a repointed version would still be a build-machine path baked into every
binary, would mask a failed staging run, and would make two rules where proximity already suffices.
`NEEDRENAME_DATA_ROOT` covers the workflows that want a specific directory.

Consequences: `engine/cmake_config.h.in` keeps only the five subdirectory-name constants, so the
header is machine-independent; `ENGINE_DEV_*`, `ENGINE_PHYSICS_SPIRV_DIR` and
`ENGINE_PHYSICS_SPIRV_CONFIG_SUBDIR` disappear; `ResolveDataDir` loses its per-category fallback
parameter and the physics loader matches.

### D4: One assembly function, three callers

`needrename_stage_runtime(TARGET PREFIX COMPONENTS DEPENDS [EXTRA_COPY src dst])` removes the prefix,
runs `cmake --install --component` for each component, copies one extra directory if asked, and
touches a stamp. The development product, the Python development package and (conceptually) a
distribution install are the same rule; a component added for a distribution appears in the
development product without another edit.

The alternative - a hand-written copy list per consumer - is what made the three drift apart before.

### D5: Staging is split by rate of change, and staging is authoritative

Four commands: built-in assets (copy + bake), sample assets, project templates, test fixtures; plus
physics modules from `engine/Physics`. Each removes its destination and copies, so a deleted source
cannot leave a stale copy, and each depends on the files it reads, so a no-op build runs nothing -
in particular no Tint. Built-in assets were previously copied together with the sample content and
project templates in the bake command, which made a sample-asset edit re-bake every shader.

Physics staging is unconditional: the runtime resolves `spirv/` through the data root for every
backend, so a Vulkan product without it would have no modules. Its per-configuration bake
subdirectory is flattened away here, which is why the runtime needs no configuration name.

### D6: Install reads staging, never the checkout

Every data `install()` sources `stage/<config>`. That makes staging the single decision point for a
category's form and contents, keeps the "build preprocesses, install copies" split literal, and means
an install and the assembled product carry identical bytes by construction.

### D7: Tests run the product

`anro_add_test` registers `output/<config>/bin/<test>` and sets `NEEDRENAME_DATA_ROOT=` (empty), so
every test exercises the proximity rule and the assembled data, and an inherited value cannot mask a
broken layout. Tests do not each depend on the assembly target - that would be a target-level cycle
with the assembly's dependency on the test binaries; the product is part of the default build, so a
test binary and its product are both present after `cmake --build`.

The redundant `mrt_test_staged_builtin_assets` is deleted: with the product as the only data root,
its explicit `NEEDRENAME_DATA_ROOT` run is what every other test now does.

### D8: The two GLSL-compiling tests name the source tree

`shader_refl_test` and `builtin_shader_compile_test` compile built-in GLSL sources, which a
baked-form data root deliberately does not carry. They receive
`NEEDRENAME_SOURCE_BUILTIN_ASSETS` and pass `<it>/shaders/include` to `ShaderCompiler`, which is the
one seam that adds a system include path. The engine's own includer is left alone: a runtime that
ships baked archives has nothing to compile.
