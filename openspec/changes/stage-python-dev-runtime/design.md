## Context

See proposal.md - Why. The normal build already places `_physicsapp` and engine shared libraries in `bin/<Config>`, stages built-in assets in `stage/<Config>`, and builds physics SPIR-V per configuration. The Python package already supports a bundled `bin/` plus `share/needrename/` layout and can load the extension from `share/needrename/python/`. The install rules are the authoritative inventory of runtime binaries and data.

## Goals / Non-Goals

**Goals:**

- Reuse existing build and install artifacts without changing the Python import resolver or wheel packaging contract.
- Give single-config and multi-config builds the same target names and configuration-relative layout.
- Keep test execution registered with CTest while offering a build-and-run convenience target.
- Avoid making Python-only builds depend on unrelated C++ example executables.

**Non-Goals:**

- Supporting arbitrary Python arguments through native build-tool command lines.
- Replacing direct wheel installation/testing workflows or custom layouts that use the existing environment overrides.
- Changing engine data-root resolution or producing a second wheel as part of the normal build.

## Decisions

### Stage a package-shaped prefix per configuration

Define the development root as `${CMAKE_BINARY_DIR}/python/$<CONFIG>` and stage the package at `${root}/needrename_sim`. Its `bin/` and `share/needrename/` directories use the existing bundled-location resolver; the extension and stub may remain in `share/needrename/python/`, which the resolver already supports. Consumers set `PYTHONPATH` to `${root}` only.

The alternative of retaining source-tree imports and staging only data would avoid Python-source copies but would still require separate runtime and data variables in every caller. Copying all DLLs into the normal `bin/` output instead would let different consumers overwrite or mix configurations.

### Reuse install components for assembly

`python_runtime` will depend on `generate_python_stubs` and `builtin_assets_stage`, then invoke `cmake --install` for `runtime` and `apps-data` with the package directory as `--prefix`, and copy the Python sources into that same directory. Install invocations use `--config $<CONFIG>` so multi-config builds cannot select another configuration.

Move example executables from `runtime` to a new `examples` component. Otherwise a component install during a Python-only target build could request example binaries that were never built. An all-component install and wheel packaging retain their existing contents and behavior.

The target participates in `ALL` whenever the Python subdirectory is enabled. Its install commands may run on subsequent builds, but existing staged-asset and compilation dependencies remain incremental; installation should leave unchanged files up to date rather than recompiling them.

### Use one test definition for CTest and the convenience target

Under `ENGINE_BUILD_TESTS=ON`, register `python_bindings` with CTest. The test invokes `${Python3_EXECUTABLE} -m pytest app/python/tests` with `PYTHONPATH` set exclusively to the selected staged root. `python_test` depends on `python_runtime` and invokes CTest with the matching configuration and test-name filter. This avoids separate pytest command/environment definitions.

Remove build-tree and `py-runtime` auto-detection from `conftest.py` only where it conflicts with the explicit staged flow; retain support for installed wheels and direct custom runs covered by the existing import-discovery tests. The source-tree failure tests must continue to control their own subprocess environment.

The alternative of running pytest directly in both places would duplicate the environment and allow the two entry points to drift. CTest itself will not build missing artifacts; `python_test` is the supported build-and-run entry point.

### Provide fixed example targets and one URDF override

Add `python_example_rl`, `python_example_headless`, and `python_example_windowed`, all depending on `python_runtime` and using `${Python3_EXECUTABLE}` with the staged `PYTHONPATH`. Default the shared `NEEDRENAME_PYTHON_EXAMPLE_URDF` cache path to `app/python/tests/data/minimal_robot.urdf`. Give RL a short deterministic run and headless a small frame count; the windowed target remains interactive. Headless frames go to `${CMAKE_BINARY_DIR}/python-examples/$<CONFIG>/frames`.

Arbitrary example arguments remain a direct-Python workflow. Native build tools do not provide a portable interface for forwarding target-specific arguments.

### Update developer entry points

Document the new targets and the single-`PYTHONPATH` manual workflow in `app/python/README.md`. Replace local VS Code runtime/data variables with the staged root and, where supported by the existing tasks setup, build `python_runtime` before debugging. Do not change wheel build documentation except to distinguish its separate build tree from the development runtime.

## Risks / Trade-offs

- [Install-time staging can fail if a runtime install rule references an artifact not reached by target dependencies] → Keep runtime installation limited to actual runtime artifacts and verify `python_runtime` from a clean build tree.
- [The staged package duplicates DLLs and data for each configuration] → Accept the disk cost for isolation and simple discovery; rely on incremental compilation and unchanged-file installation.
- [Deleted source files can remain in an install-style staging directory] → Use clean-tree verification and document that cleaning the build tree removes stale staged content; do not delete source or user output directories during staging.
- [A globally installed `needrename_sim` can mask the intended build] → Set `PYTHONPATH` explicitly and exclusively in CTest, example targets, and documented debug settings.
- [CTest alone does not build the staged runtime] → Document `python_test` as the one-command workflow and keep the runtime in the default build for developers who invoke CTest separately.

## Migration Plan

1. Add staging and runner targets while leaving install and wheel layouts operational.
2. Switch tests, documentation, and local debugger settings to the staged runtime.
3. Remove `build/py-runtime` discovery and verify a fresh build without that prefix.
4. Roll back by reverting the build/test configuration changes; staged files are disposable build outputs and require no data migration.
