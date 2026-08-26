## Context

`PhysicsApp` (app/physics) is a C++20 DLL wrapping the engine's Vulkan + GPU physics pipeline with a documented lifecycle (Building → CommitScene → Drive). It exposes COM-space read/write state (`BodyState`, `SetBodyValue`) plus joints, actuators, URDF import, and optional render readback. The project already runs a Python virtualenv at `build/parser_env` for the AnnoRefl libclang reflection parser, and nanobind 3.0.0 is vendored as a git submodule under `third_party/nanobind` (currently missing its `ext/robin_map` dependency).

This change adds an optional Python binding (`app/pyengine`) so the app can be scripted for RL/control experiments without recompiling C++. The public Python API presents body-origin coordinates; the internal binding mirrors the C++ API exactly (COM viewpoint).

## Goals / Non-Goals

**Goals:**
- Script the full `PhysicsApp` surface from Python (all methods, structs, enums, actuator hierarchy including Python subclassing).
- Present a body-origin view: users never see COM offsets in reads or writes; conversion is transparent and call-order-independent.
- Keep the binding optional: default builds are untouched; `debug-py` / `release-py` presets enable it.
- Reuse a single Python environment (`build/.venv`) for reflection, binding build, and tests.
- Windows (MSYS2 CLANG64) support, with guard points so other platforms can be added later.

**Non-Goals:**
- Packaging/`pip install` distribution, wheels, or `setuptools` integration (no install rules).
- Non-Windows support in this change.
- Free-threaded / multi-threaded use of the app (single-threaded only, documented).
- Changing the C++ API surface beyond the single `AddActuator` ownership change.
- A `numpy`-free fallback path or stable-ABI builds (documented contingency only).

## Decisions

### D1: Two-layer architecture — `_native` (COM mirror) + `pyengine` (body view)

`_native.pyd` is a nanobind module that mirrors every C++ declaration 1:1 (enums, structs, classes, methods — COM semantics, exactly as documented in `PhysicsApp.h`). The `pyengine` package imports `_native` and wraps it in a composition class `App` that applies coordinate conversions and deferred writes.

Rationale: keeps the binding verifiable against the C++ docs, isolates all conversion logic in pure Python (easily unit-tested without GPU), and lets advanced users drop to `_native` if needed. Alternatives considered: (a) doing conversions in C++ binding code — rejected (mixes two viewpoints in one layer, harder to test); (b) monkey-patching `_native.PhysicsApp` — rejected (dirty, state leaks into the module).

### D2: Python environment unification — `build/.venv`, env-var-first discovery

- Rename `build/parser_env` → `build/.venv` (update `ANROREFL_PARSER_ENV_DIR` default in `third_party/CMakeLists.txt`).
- Python discovery order: `$ENV{PYENGINE_PYTHON}` → the interpreter AnnoRefl already cached (`Python3_EXECUTABLE`, the `.venv`'s python) → `find_package(Python3 COMPONENTS Interpreter Development)`.
- Build and tests always use the same discovered interpreter (self-consistent CPython version ABI).
- Deleted `.venv` is recreated automatically by AnnoRefl's existing configure-time logic.

Rationale: one interpreter for reflection, nanobind build (headers/libs via sysconfig of the base install), and pytest. Env var gives machines with a non-default Python a documented override (VS Code `cmake.environment` example in docs). ABI strategy: mingw-built extension + mingw-built Python (MSYS2 clang64) is the clean path; if discovery lands on an MSVC-built Python (e.g. conda), the link step may fail — contingency: prefer clang64 Python via the env var, or (last resort) gendef/dlltool + stable ABI. No stable ABI by default.

### D3: Deferred writes flushed at `step()` — order-independent body-frame writes

All high-level setters (`set_position`, `set_rotation`, `set_linear_velocity`, `set_angular_velocity`, `set_force`, `set_torque`) only record per-body pending values; `step()` flushes them to C++ `SetBodyValue` once, using final values:

```
R_final = pending_rotation or last-readback R
ω_final = pending_angular_velocity or last-readback ω
com_pos = pos + R_final · c          (c = cached body-local COM offset)
v_com   = lin + ω_final × (R_final · c)
rotation / angular_velocity / force / torque: direct
```

Rationale: conversions depend on the orientation/angular velocity in effect at the next step. A naive "pending rotation only" shadow was proven wrong in review for the order `set_linear_velocity → set_pose` (velocity conversion frozen with the old R). Deferring all writes to a single flush point makes every call order and subset equivalent by construction. This matches the C++ contract ("write applies at next Step") — C++ code is untouched. Reads always return the last-stepped state (never mix pending values — that would require simulating the integrator). Setters and getters fail fast (raise `RuntimeError`) if called before `CommitScene`, tracked by a wrapper phase flag set in `commit_scene()`.

### D4: Coordinate conversions are confined to position and linear velocity

- Rotation and angular velocity are identical in COM and body frames (direct pass-through).
- `ExternalForce` / `ExternalTorque` are passed through unchanged with COM semantics documented — no torque compensation (`r × F`). Rationale: the torque channel is owned by actuators on actuated joints (silent loss), compensation needs shadow state, and treating force as COM-applied is a defensible, simple contract. `get_com_offset(body_id)` (body-local, static after `CommitScene`, cached from `GetBodyStates`' `com_offsets`/`slot_indices`) lets advanced users do their own frame math.

### D5: Actuator ownership — C++ `unique_ptr` → `shared_ptr` (BREAKING)

`AddActuator` takes `std::shared_ptr<Actuator>`; `PhysicsApp` stores `std::vector<std::shared_ptr<Actuator>>`. Rationale: Python-subclassed actuators are nanobind trampoline objects; `unique_ptr` ownership transfer leaves the Python object dangling when C++ deletes it (use-after-free). `shared_ptr` is a first-class nanobind type — both sides hold refs, destruction is safe, no keep_alive tricks. The change surface is small: signature, storage, and the two existing actuator tests (`make_unique` → `make_shared`). This modifies the completed `physicsapp-joint-actuators` spec (recorded as a delta).

### D6: DLL resolution — `PYENGINE_DLL_DIR` + `os.add_dll_directory`

`pyengine/__init__.py` reads `PYENGINE_DLL_DIR`; if set, calls `os.add_dll_directory(path)` before `from . import _native`; falls back to adding the package's own directory (the build copies the package and `.pyd` into `build/<cfg>/bin/pyengine/`, co-located with `PhysicsApp.dll`, `Engine*.dll`, `SDL3.dll`, etc.). Guarded to Windows (`os.path.exists` on the attribute), keeping Linux/macOS a one-line extension point later. Rationale: the DLL set is a build artifact path, not knowable at module import time; an env var is explicit, scriptable, and matches how the project already documents MSYS2 env vars. No install rules → no sysconfig/pip plumbing.

### D7: Type casting strategy

- glm `vec3`/`quat`: custom nanobind casters — accept any `(3,)`/`(4,)` sequence (tuple/list/numpy, float32/64), produce numpy `float32` on output. Quaternions are xyzw everywhere (documented prominently).
- Batch reads: `nb::ndarray` (numpy caster) with **copies** — the C++ spans point into staging buffers valid only until the next `Step`. Copy semantics kill the lifetime trap; batch sizes are small.
- `std::optional<JointLimits>` → `None`/object; `std::unordered_map<std::string, ...>` → `dict`; `std::filesystem::path` → `str`; spans → numpy arrays; `RenderOutput.pixels` → copied `(H, W, 4)` uint8 buffer; `BodyId`/`JointId` → `int`.
- Exceptions: nanobind auto-maps (`std::logic_error` → `RuntimeError`, `std::out_of_range` → `IndexError`, `std::invalid_argument` → `ValueError`); high level passes through, docs carry a mapping table. No custom exception hierarchy.

### D8: GIL — no special handling

nanobind defaults to holding the GIL; we do not add `gil_scoped_release`. Rationale: `Step()` may call back into Python through an actuator trampoline's `ComputeTorque`, which requires the GIL — releasing it risks deadlock. Single-threaded usage (the app's contract) means holding the GIL has no practical cost. Documented.

### D9: Build integration

- `option(PYENGINE_BUILD "..." OFF)` in `app/pyengine/CMakeLists.txt`; configure presets `debug-py` / `release-py` inherit `debug` / `release` with `PYENGINE_BUILD: ON`.
- The `_native` module target uses `nanobind_add_module` with `LIBRARY_OUTPUT_DIRECTORY` overridden to `build/<cfg>/bin/pyengine/`; the `python/pyengine/` sources are copied to the same directory by a build-time step.
- Configure-time requirement install (mirroring AnnoRefl): when enabled, if `numpy` is missing from the discovered interpreter, run `pip install -r app/pyengine/requirements.txt` (`numpy`, `pytest`; unpinned with lower bounds); failure is `FATAL_ERROR`. Contingency note in docs: for a mingw-base interpreter, PyPI wheels are MSVC-built — fall back to `pacman -S mingw-w64-clang-x86_64-python-numpy`.
- CTest: register pytest invocations via the discovered interpreter; GPU tests carry the `gpu` label; pure-logic tests need no GPU. Prerequisite: `git submodule update --init --recursive third_party/nanobind` (missing `ext/robin_map` causes nanobind's CMake FATAL_ERROR).

## Risks / Trade-offs

- **Mingw extension vs MSVC-built Python (conda) ABI** → If discovery lands on conda/MSVC Python, GNU linking against `python313.lib` may fail; mitigation: env var to force clang64 Python; documented last-resort gendef/dlltool + stable ABI. Not a runtime mystery — it fails at link time if at all.
- **`libclang` (AnnoRefl requirement) on Python 3.14** → The recreated `.venv` may target 3.14; `libclang` is an sdist. Mitigation: spike task before/at implementation start; if it fails, keep `.venv` on an older interpreter and set `PYENGINE_PYTHON` to a 3.14 (or same-version) interpreter.
- **nanobind 3.0.0 on MSYS2 clang64 is less battle-tested than 2.12 (MSYS2's packaged version)** → Spike: build a minimal `NB_MODULE` with the submodule under clang64 early; MSYS2 packaging of 2.12 shows the combination works in principle.
- **Deferred write model surprises users who read before stepping** → Explicitly documented ("reads return the last-stepped state"); fail-fast phase errors prevent the worse trap (writing before commit).
- **Python-subclassed actuator lifetime** → shared_ptr + trampoline; dedicated GPU smoke test with a custom actuator subclass locks the path.
- **Numpy copy overhead on batch reads** → Batch sizes are small (body counts); copies are intentional for lifetime safety.
- **`.venv` rename touches the reflection flow** → Behavior unchanged (auto-create + pip install); any 3.14-specific `libclang` failure is caught by the spike before other work depends on it.

## Migration Plan

1. Spike A: `libclang` install on a 3.14 venv; Spike B: minimal nanobind module with the submodule under clang64 (resolves the two environment risks before build work).
2. Rename `build/parser_env` → `build/.venv`, update `ANROREFL_PARSER_ENV_DIR` default, reconfigure, confirm reflection still generates.
3. Land the C++ `AddActuator` shared_ptr change (C++-only, independent of the binding; existing actuator tests updated).
4. Implement `_native` binding → `pyengine` package → tests (pure-logic first, then GPU smoke).
5. Docs (`docs/pyengine.md`, README entry, VS Code env example).

Rollback: `PYENGINE_BUILD=OFF` restores previous behavior; the C++ shared_ptr change is a small, self-contained diff (update the two actuator tests alongside it).

## Open Questions

- Whether to keep compound `set_pose`/`set_velocities` sugar — resolved: no (fine-grained only).
- Whether to add `to_body`/`to_com` helpers — resolved: no, `get_com_offset` suffices.
