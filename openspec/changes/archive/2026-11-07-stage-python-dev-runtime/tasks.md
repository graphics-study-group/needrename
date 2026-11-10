## 1. Install and runtime staging

- [x] 1.1 Move example executables from the `runtime` install component to `examples` and verify the component definitions show an unchanged all-component layout without examples in `runtime`.
- [x] 1.2 Add the per-configuration `python_runtime` default target using the `runtime` and `apps-data` install rules plus Python source copying; build it and verify the package, extension/stub, shared libraries, built-in assets, physics SPIR-V, and projects exist under `python/<Config>/needrename_sim`.
- [x] 1.3 Verify a real import using only `PYTHONPATH=<build>/python/<Config>` with the runtime/data environment overrides unset, and verify another configuration stages into a separate directory.

## 2. Test and example entry points

- [x] 2.1 Register `python_bindings` with CTest and add `python_test` when `ENGINE_BUILD_TESTS=ON`; run `python_test` and the filtered CTest entry and verify both execute the staged package.
- [x] 2.2 Verify an `ENGINE_BUILD_TESTS=OFF` configure has no Python test target or CTest registration while retaining runtime and example targets.
- [x] 2.3 Add the RL, headless, and windowed example targets with the shared URDF cache variable and build-tree output directory; run the RL and headless targets and verify simulation output and generated frames, then inspect the windowed target command without requiring an interactive run.

## 3. Migration and documentation

- [x] 3.1 Remove legacy `build/py-runtime` and build-tree guessing from the official pytest development path while retaining wheel and explicit-environment workflows; run import-discovery and import-failure tests to verify their contracts remain intact.
- [x] 3.2 Update `app/python/README.md` and local VS Code debug settings to use only the staged `PYTHONPATH`, adding a runtime pre-launch build where supported; verify no current documentation or debug configuration refers to `build/py-runtime`.

## 4. End-to-end verification

- [x] 4.1 Reconfigure with the documented platform environment, build the standard default target, and verify it creates the development runtime without a separate install step.
- [x] 4.2 Run the standard CTest preset and verify the new Python test plus existing tests pass; inspect the working tree afterward and remove only generated files created by verification that are not part of the change.
