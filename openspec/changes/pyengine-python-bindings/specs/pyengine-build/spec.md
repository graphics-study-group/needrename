# pyengine-build

## ADDED Requirements

### Requirement: pyengine builds are optional and preset-enabled

The build system SHALL provide an option `PYENGINE_BUILD` defaulting to `OFF`. When `OFF`, no pyengine targets, Python discovery, or dependency installation SHALL occur, and the build SHALL be byte-identical in behavior to a pre-change build.

The CMake presets SHALL include `debug-py` and `release-py` configure presets that inherit `debug` and `release` respectively and set `PYENGINE_BUILD: ON`.

#### Scenario: Default build excludes pyengine
- **WHEN** the `debug` preset is configured and built
- **THEN** no `_native` module, `pyengine` package, or Python dependency installation occurs

#### Scenario: Preset build includes pyengine
- **WHEN** the `debug-py` preset is configured
- **THEN** `PYENGINE_BUILD` is `ON`, and configuring the `debug-py` build produces the `_native` module target

### Requirement: Python environment is unified under build/.venv

The shared Python virtualenv SHALL live at `build/.venv`. The `ANROREFL_PARSER_ENV_DIR` default SHALL point there (replacing the old `build/parser_env` default), so the AnnoRefl reflection parser, the nanobind build, and the pyengine tests all use one interpreter.

The venv SHALL be created and populated automatically at configure time by the existing AnnoRefl logic when it is missing.

#### Scenario: Reflection env path renamed
- **WHEN** a fresh configure runs with default cache variables
- **THEN** `ANROREFL_PARSER_ENV_DIR` resolves to `build/.venv` and the venv is created there if absent

### Requirement: Python interpreter discovery is deterministic

When `PYENGINE_BUILD` is `ON`, the interpreter used for building `_native` (headers/libs) and for running tests SHALL be selected in this order:
1. The `PYENGINE_PYTHON` environment variable, if set and valid;
2. The interpreter already cached by AnnoRefl's `find_package(Python3)` (the `.venv` python);
3. `find_package(Python3 COMPONENTS Interpreter Development)`.

The same interpreter SHALL be used for both compile-time artifacts and test execution, guaranteeing CPython version ABI consistency.

#### Scenario: Env var overrides discovery
- **WHEN** `PYENGINE_PYTHON` is set to a valid python executable
- **THEN** that executable is used for building and testing, regardless of what `find_package` would otherwise find

#### Scenario: Default falls back to the cached venv interpreter
- **WHEN** `PYENGINE_PYTHON` is unset
- **THEN** the interpreter cached by AnnoRefl (the `.venv` python) is used for build and tests

### Requirement: Python dependencies install at configure time

When `PYENGINE_BUILD` is `ON`, the build SHALL ensure `numpy` and `pytest` are installed in the discovered interpreter, installing `app/pyengine/requirements.txt` at configure time if `numpy` is missing (mirroring the AnnoRefl venv setup). A failed installation SHALL fail configuration with a clear error.

The requirements file SHALL declare lower bounds only, not exact pins.

#### Scenario: Missing numpy triggers install
- **WHEN** configuration runs with `PYENGINE_BUILD: ON` and `numpy` is absent from the discovered interpreter
- **THEN** pip installs the requirements into that interpreter, and configuration continues only on success

### Requirement: Python tests register with CTest

The change SHALL register the pyengine pytest suite with CTest using the discovered interpreter. Tests that require a GPU (PhysicsOnly smoke, Offscreen pixel readback) SHALL carry the `gpu` label, matching the existing physics test convention. Pure-logic tests SHALL carry no GPU label and SHALL run without a GPU.

#### Scenario: ctest runs pure-logic tests headlessly
- **WHEN** `ctest --preset debug-py` runs with `-L "!gpu"` on a machine without a GPU
- **THEN** all pyengine pure-logic tests pass without initializing Vulkan

#### Scenario: GPU tests are labeled
- **WHEN** `ctest --preset debug-py -N` lists pyengine tests
- **THEN** PhysicsOnly and Offscreen tests appear under the `gpu` label

### Requirement: Platform scope is Windows with guard points

The pyengine build and runtime SHALL target Windows (MSYS2 CLANG64) only. Platform-specific runtime code (notably `os.add_dll_directory`) SHALL be guarded so it is a no-op or cleanly disabled on non-Windows platforms, without otherwise breaking the package import on those platforms.

#### Scenario: Import on non-Windows does not crash on DLL setup
- **WHEN** `pyengine` is imported on a platform without `os.add_dll_directory`
- **THEN** the DLL-directory step is skipped and import proceeds through the fallback path
