## 1. Pre-flight: environment spikes and hygiene

- [ ] 1.1 Update nanobind submodule recursively: `git submodule update --init --recursive third_party/nanobind`; verify `third_party/nanobind/ext/robin_map/include/tsl/robin_map.h` exists.
- [ ] 1.2 Spike: recreate a fresh venv from the clang64 Python 3.14 (`python -m venv`) and run `pip install libclang mako`; record whether the AnnoRefl requirements install on 3.14. If it fails, record the fallback (keep `.venv` on an older interpreter and pin `PYENGINE_PYTHON`).
- [ ] 1.3 Spike: build a minimal nanobind module (single `NB_MODULE` returning an int) with the submodule under clang64, load it with the target Python, and confirm it imports. Resolves the nanobind 3.0 + mingw risk before any binding work.
- [ ] 1.4 Rename `build/parser_env` → `build/.venv`; update `ANROREFL_PARSER_ENV_DIR` default in `third_party/CMakeLists.txt`; reconfigure and confirm reflection code generation still works.

## 2. C++ API change: AddActuator shared_ptr

- [ ] 2.1 Change `PhysicsApp::AddActuator` signature in `app/physics/PhysicsApp.h` from `std::unique_ptr<Actuator>` to `std::shared_ptr<Actuator>`; update the Doxygen ownership wording.
- [ ] 2.2 Update `PhysicsApp.cpp` actuator storage (`std::vector<std::unique_ptr<Actuator>>` → `std::vector<std::shared_ptr<Actuator>>`) and `AddActuator` implementation (drop `std::move`, keep the null/duplicate/phase/invalid-id checks).
- [ ] 2.3 Update `test/app/physics/physics_app_actuator_stand_test.cpp` and `physics_app_actuator_unit_test.cpp`: `std::make_unique<...>` → `std::make_shared<...>`.
- [ ] 2.4 Build and run the two actuator C++ tests (`physics_app_actuator_*`) to confirm the API change is behavior-neutral.

## 3. Build integration: option, presets, discovery, deps

- [ ] 3.1 Create `app/pyengine/CMakeLists.txt` with `option(PYENGINE_BUILD "..." OFF)`; when OFF, the subdirectory adds no targets; when ON, perform Python discovery (order: `$ENV{PYENGINE_PYTHON}` → AnnoRefl-cached `Python3_EXECUTABLE` → `find_package(Python3 COMPONENTS Interpreter Development)`) and `FATAL_ERROR` with a clear message if none is usable.
- [ ] 3.2 Wire `add_subdirectory(pyengine)` (guarded by nothing else) into `app/CMakeLists.txt`; ensure the OFF path adds zero behavior.
- [ ] 3.3 Add `debug-py` / `release-py` configure presets to `CMakePresets.json` inheriting `debug` / `release` with `PYENGINE_BUILD: ON`.
- [ ] 3.4 Create `app/pyengine/requirements.txt` (`numpy>=...`, `pytest>=...`, lower bounds only).
- [ ] 3.5 Implement configure-time requirement install (mirroring AnnoRefl): when `PYENGINE_BUILD` is ON and `numpy` is missing from the discovered interpreter, `pip install -r app/pyengine/requirements.txt`; failure → `FATAL_ERROR`.
- [ ] 3.6 Ensure the `_native` module target emits into `build/<cfg>/bin/pyengine/` (`LIBRARY_OUTPUT_DIRECTORY` override) and the `python/pyengine/` sources copy to the same directory via a build-time step.

## 4. Native binding: _native module

- [ ] 4.1 Create `app/pyengine/PyPhysicsAppBinding.cpp` with `NB_MODULE(_native)`; bind enums `AppMode`, `BodyField` and all structs (`CreateInfo`, `BodyState`, `JointState`, `JointLimits`, `BodyStatesView`, `RenderOutput`, `UrdfImportConfig`, `UrdfImportResult`, `JointBodyPair`, `BoxDesc`, `SphereDesc`, `CylinderDesc`, `FixedJointParams`, `HingeJointParams`, `DirectionalLightParams`).
- [ ] 4.2 Bind `PhysicsApp` class: `Create` factory (returns the class, non-copyable), all Building/Drive/both-phase methods with matching signatures; verify every public method is present.
- [ ] 4.3 Add glm casters: accept any `(3,)`/`(4,)` sequence for `vec3`/`quat`/`vec4`; quaternion xyzw; float32 outputs.
- [ ] 4.4 Add standard-library conversions: `std::optional<JointLimits>` → `None`/object, `unordered_map` → `dict`, `filesystem::path` ↔ `str`, `BodyId`/`JointId` → `int`.
- [ ] 4.5 Expose `BodyStatesView` spans as copied numpy arrays (int32 slot indices, float32 positions/rotations/velocities) and `RenderOutput` as copied `(H, W, 4)` uint8 + `width`/`height`/`frame_id`; never expose raw pointers.
- [ ] 4.6 Bind `Actuator` (abstract, with trampoline for Python subclassing of `compute_torque`), `PdActuator`, `DcMotorActuator`; bind `AddActuator` accepting `std::shared_ptr<Actuator>`.
- [ ] 4.7 Confirm nanobind's automatic exception translation at the boundary (`logic_error`→`RuntimeError`, `out_of_range`→`IndexError`, `invalid_argument`→`ValueError`, `runtime_error`→`RuntimeError`) with a quick manual check; no re-wrapping.
- [ ] 4.8 Verify the built `_native.pyd` lands inside the `pyengine` package dir and imports with `build/<cfg>/bin` on the DLL path.

## 5. Public package: pyengine

- [ ] 5.1 Create `app/pyengine/python/pyengine/__init__.py`: read `PYENGINE_DLL_DIR` → `os.add_dll_directory` (guarded for non-Windows), fall back to package directory, then `from . import _native`; re-export all `_native` classes/structs/enums with docstrings.
- [ ] 5.2 Implement `create_app(...)` factory and `App` wrapper class mirroring the full method surface (all methods, no omissions); track a wrapper phase flag flipped in `commit_scene()`.
- [ ] 5.3 Implement COM offset cache: after `commit_scene`, call `_native.GetBodyStates()`, build `BodyId → com_offset` map from `slot_indices`/`com_offsets`; expose `get_com_offset(body_id)` returning body-local float32 `(3,)`; fail fast before commit.
- [ ] 5.4 Implement body-frame reads: `get_body_state` converts position (`pos_com − R·c`) and linear velocity (`v_com − ω×(R·c)`); rotation/angular velocity pass through.
- [ ] 5.5 Implement deferred writes: six fine-grained setters record pending values per body; `step()` flushes with final values (`R_final`, `ω_final` rules per design D3) then calls `_native.Step()`, then clears pending state; setters fail fast before commit.
- [ ] 5.6 Implement `get_body_states()` returning `BodyStatesBatch` dataclass (BodyId-indexed dense float32 arrays: `body_ids`, `positions`, `rotations`, `linear_velocities`, `angular_velocities`), body-origin converted; no COM offsets in the batch.
- [ ] 5.7 Implement `get_render_output()` returning `RenderFrame` dataclass (copied `(H,W,4)` uint8 `pixels`, `width`, `height`, `frame_id`).
- [ ] 5.8 Enforce uniform numeric conventions in all wrapper methods: accept any `(3,)`/`(4,)` sequence, output float32 numpy, xyzw quaternions.

## 6. Tests

- [ ] 6.1 Pure-logic test file (no GPU): coordinate conversion math (position/velocity with and without COM offset), deferred-write order-equivalence (all setter orders and subsets), fail-fast phase errors, `get_com_offset` caching, exception mapping, input-convention equivalence — using fake/mock native objects where the wrapper permits injection.
- [ ] 6.2 PhysicsOnly GPU smoke test: `create_app(PhysicsOnly)`, add box/sphere/cylinder, commit, step, read state; includes a Python-subclassed `Actuator` on a hinge joint (verify `compute_torque` is called and torque applied).
- [ ] 6.3 Offscreen GPU test: enable render readback, render, assert `RenderFrame` pixels shape/dtype and `frame_id`; assert `get_render_output` raises in `PhysicsOnly` mode.
- [ ] 6.4 Register the pytest suite with CTest using the discovered interpreter (`app/pyengine` tests); GPU tests get the `gpu` label; pure-logic tests run headless; add to test preset chain so `ctest --preset debug-py` runs them.
- [ ] 6.5 Run the full suite under `debug-py`; confirm pure-logic tests pass and GPU tests pass on a GPU machine.

## 7. Docs

- [ ] 7.1 Create `docs/pyengine.md`: environment variables (`PYENGINE_PYTHON`, `PYENGINE_DLL_DIR`), VS Code `cmake.environment` example (mirroring the MSYS2 env-var doc style), build/run instructions (`debug-py` preset, ctest), numpy fallback note for mingw-base interpreters, body-frame semantics, deferred-write/read semantics, quaternion xyzw convention, C++ exception mapping table.
- [ ] 7.2 Add a short README section with an import example and links to `docs/pyengine.md`.
- [ ] 7.3 Verify `.gitignore` covers `.venv` and any build output (confirm `build/` is ignored); the local `.vscode/settings.json` gains `PYENGINE_PYTHON` (machine-local, gitignored).
