## Why

`PhysicsApp` (app/physics) exposes a complete physics simulation API in C++, but there is no scripting surface: reinforcement-learning experiments, robot-control prototyping, and headless automation currently require writing C++ test programs and recompiling. A Python binding (`pyengine`) makes the app scriptable end-to-end, with a Python-level API that presents body-origin coordinates so users never need to reason about center-of-mass offsets.

## What Changes

- **Add `app/pyengine/`** with two layers:
  - `_native`: a nanobind module mirroring the complete `PhysicsApp` C++ API (COM viewpoint), used internally.
  - `pyengine` package: the public Python API presenting body-origin coordinates, with deferred writes flushed at `step()`.
- **Add optional CMake build**: `PYENGINE_BUILD` option (default OFF) + `debug-py` / `release-py` configure presets that inherit `debug` / `release`.
- **Unify the Python environment**: rename `build/parser_env` to `build/.venv` (AnnoRefl default changes too); Python discovery order is `PYENGINE_PYTHON` env var → AnnoRefl-cached interpreter → `find_package(Python3)`; build and test always use the same interpreter.
- **Auto-install Python deps** (`numpy`, `pytest` via `app/pyengine/requirements.txt`) at configure time when the module is enabled, mirroring the existing AnnoRefl venv setup.
- **High-level API design** (all decisions settled in exploration):
  - BodyId/JointId are plain `int`; descriptor structs are re-exported nanobind classes.
  - Fine-grained setters (`set_position`, `set_rotation`, `set_linear_velocity`, `set_angular_velocity`, `set_force`, `set_torque`) are deferred and flushed at `step()`, so any call order is equivalent.
  - Reads always return the last-stepped state; both getters and setters fail fast before `CommitScene`.
  - `get_com_offset(body_id)` exposes the static body-local COM offset; `get_body_states()` returns a `BodyStatesBatch` dataclass with BodyId-indexed dense float32 arrays; `GetRenderOutput` returns a `RenderFrame` dataclass with a copied `(H, W, 4)` uint8 pixel buffer.
  - All vec3/quat parameters accept any `(3,)`/`(4,)` sequence; quaternions are xyzw.
- **BREAKING (C++ API)**: `PhysicsApp::AddActuator` changes from `std::unique_ptr<Actuator>` to `std::shared_ptr<Actuator>` so Python-subclassed actuators (trampoline objects) share ownership safely across the boundary.
- **DLL resolution**: `pyengine/__init__.py` reads `PYENGINE_DLL_DIR` and calls `os.add_dll_directory` before importing `_native`, falling back to the package's own directory.
- **Testing**: pytest suite registered with CTest — pure-Python logic tests (conversion math, flush order-equivalence, fail-fast, exception mapping; no GPU), a PhysicsOnly smoke test including a Python-subclassed actuator, and an Offscreen pixel-readback test; GPU tests carry the `gpu` label.

## Capabilities

### New Capabilities

- `pyengine-build`: CMake option + presets, `.venv` unification, Python discovery (`PYENGINE_PYTHON` → cached interpreter → `find_package`), configure-time requirements install, Windows-only scope with cross-platform guard points, CTest registration of the pytest suite.
- `pyengine-native-binding`: the `_native` nanobind module — full mirror of the `PhysicsApp` C++ surface (enums, structs, classes, methods, actuator hierarchy with trampoline), glm type casters, span/optional/map conversions, automatic C++ exception → Python exception mapping.
- `pyengine-python-api`: the public `pyengine` package — body-origin coordinate conversions, deferred write model with `step()` flush, `BodyStatesBatch` / `RenderFrame` dataclasses, `get_com_offset`, phase fail-fast semantics, `PYENGINE_DLL_DIR` loading in `__init__.py`.

### Modified Capabilities

- `physicsapp-joint-actuators`: `AddActuator` ownership semantics change from `std::unique_ptr<Actuator>` to `std::shared_ptr<Actuator>` (signature, storage, and ownership requirements).

## Impact

- **New code**: `app/pyengine/` (CMakeLists, `_native` binding source, `python/pyengine/` package, `requirements.txt`, tests).
- **Modified code**:
  - `third_party/CMakeLists.txt` — `ANROREFL_PARSER_ENV_DIR` default `build/parser_env` → `build/.venv`.
  - `CMakePresets.json` — new `debug-py` / `release-py` configure presets.
  - `app/physics/PhysicsApp.h` / `PhysicsApp.cpp` — `AddActuator` shared_ptr change (storage container and implementation).
  - `test/app/physics/physics_app_actuator_stand_test.cpp`, `physics_app_actuator_unit_test.cpp` — `make_unique` → `make_shared`.
- **Repository hygiene**: `third_party/nanobind` submodule requires `git submodule update --init --recursive` (missing `ext/robin_map`).
- **Docs**: new `docs/pyengine.md` (environment variables `PYENGINE_PYTHON`, `PYENGINE_DLL_DIR` with a VS Code `cmake.environment` example), README entry section.
- **Dependencies**: nanobind (submodule, existing), CPython 3.x via discovery, numpy (build + runtime), pytest (tests).
- **Platform**: Windows (MSYS2 CLANG64) only for now; `os.add_dll_directory` call guarded for other platforms.
