## Why

Python development runs currently require a separate manual install into the undocumented `build/py-runtime` prefix, while tests and debugger configurations independently guess runtime and data locations. This makes a successful `_physicsapp` build insufficient for importing the package and leaves fresh checkouts with broken test and debug workflows.

## What Changes

- Add a per-configuration, self-contained Python development runtime under the active CMake build tree, including Python sources, the extension and stub, shared libraries, built-in assets, physics SPIR-V, and project templates.
- Add `python_runtime` to the default build when Python bindings are enabled, and make Python test and example targets depend on it.
- Register the Python pytest suite with CTest and add a `python_test` build target that prepares the runtime before running the same test.
- Add fixed CMake targets for the RL, headless, and windowed Python examples, using the mesh-free test URDF by default and allowing a caller-supplied URDF through a cache variable.
- Make the staged package discoverable through `PYTHONPATH` alone; retain the existing environment-variable overrides for custom layouts.
- **BREAKING**: Remove `build/py-runtime` from documented and automatic developer discovery. Existing checkouts must use the new staged runtime instead.
- Move example executables out of the `runtime` install component into an `examples` component so staging Python does not require unrelated example builds. Installing all components remains unchanged.

## Capabilities

### New Capabilities

- `python-development-runtime`: Per-configuration staging and one-command test/example workflows for Python binding development.

### Modified Capabilities

- `install-layout`: Example executables move from `runtime` to a dedicated `examples` install component; the combined install layout remains the same.

## Impact

- Python and top-level CMake build rules, install component definitions, Python test discovery, development documentation, and local VS Code debug settings.
- No changes to the wheel's public API, the engine data-root resolution chain, or installed/wheel runtime discovery semantics.
- Development runtime files are duplicated from the normal build outputs in exchange for stable discovery and configuration isolation; unchanged files should not be rebuilt unnecessarily.
