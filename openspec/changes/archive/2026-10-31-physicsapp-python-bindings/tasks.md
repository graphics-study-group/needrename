# Tasks: PhysicsApp Python Bindings

## 1. Background clear color and sky data trim

- [x] 1.1 Add `background_color` (linear rgb, default black) to `CreateInfo` and plumb it into `ComplexRenderGraphBuilder::BuildDefaultRenderGraph` as the HDR clear-color parameter (editor call site keeps its current default). Verify: `cmake --build --preset msvc-debug` succeeds for Engine/PhysicsApp/EngineEditor and the existing ctest suite passes unchanged.
- [x] 1.2 Trim the default project `projects/empty_with_sky`: drop the `m_skybox_material` reference from `assets/default_level.asset` and delete `assets/environment/` (cubemap). Verify: no cubemap texture files remain in the project directory and `physics_app_offscreen_test` passes.
- [x] 1.3 Add a C++ offscreen test asserting the rendered background: default creation reads back black pixels away from geometry; a configured background color reads back within tolerance of the configured value. Verify: new test registered in `test/app/physics/CMakeLists.txt` and passing under `ctest --preset msvc-debug`.

## 2. SetJointTorque

- [x] 2.1 Implement `SetJointTorque(JointId, float)` in `app/physics/PhysicsApp.{h,cpp}`: Drive-phase guard (`std::logic_error` before commit), `std::out_of_range` on invalid JointId, `std::logic_error` on joints with actuators, equal-and-opposite `±torque*axis_world` writes through the existing persistent external-torque path; update the class-level torque-channel doc comment. Verify: builds under `msvc-debug` and `msvc-release`.
- [x] 2.2 Add `test/app/physics/physics_app_joint_torque_test.cpp`: torque accelerates the joint in the expected direction with equal-and-opposite body torques; a once-set torque persists across steps until zeroed; error cases (before commit, invalid id, actuated joint) throw the specified exception types; actuator torque path unchanged. Verify: test passes under `ctest --preset msvc-debug`.

## 3. Binding build scaffolding

- [x] 3.1 Add `third_party/nanobind` as a git submodule and register it with the build. Verify: `git submodule status` shows the checkout and a fresh configure finds nanobind.
- [x] 3.2 Create the `app/python/` target with a `nanobind_add_module` target producing `needrename_sim._physicsapp`, linking `PhysicsApp`, using the project's `Python3_EXECUTABLE`; stage the `.pyd` beside the engine DLLs (or into the package tree) for dev runs. Verify: `cmake --build --preset msvc-debug` produces `_physicsapp*.pyd` and a dev-mode import succeeds with `NEEDRENAME_RUNTIME_DIR`/`NEEDRENAME_DATA_ROOT` set.
- [x] 3.3 Implement the import-failure path: with no env vars and no bundled location, import raises an error naming `NEEDRENAME_RUNTIME_DIR` and `NEEDRENAME_DATA_ROOT`. Verify: a small script asserting the message is added to the Python test suite skeleton and passes.

## 4. Binding surface

- [x] 4.1 Bind value types: `CreateInfo`, `AppMode`, `BodyField`, box/sphere/cylinder descs, joint params, light params, `BodyState`/`JointState`/`JointLimits`/`BodyStatesView`/`RenderOutput`, `UrdfImportConfig`/`UrdfImportResult`/`JointBodyPair`, and glm `vec3`/`quat`/`vec4` conversions. Verify: types construct from Python with keyword arguments mirroring the C++ descs.
- [x] 4.2 Bind `PhysicsApp` methods with exception translators (`std::logic_error`→`RuntimeError`, `std::invalid_argument`→`ValueError`, `std::out_of_range`→`IndexError`, others→`RuntimeError`). Verify: Python wrong-phase and invalid-handle tests raise the mapped types.
- [x] 4.3 Zero-copy readback: `GetBodyStates` SoA arrays and `GetRenderOutput` pixels as `nb::ndarray` views with the app as owner/base plus a `step_id` on state views. Verify: tests assert shared memory (values change in place after `step`), memory safety after app destruction while a view is held, and `(H, W, 4) uint8` pixel views.
- [x] 4.4 Release the GIL on `step`, `render_next_frame`, `get_render_output`, `load_urdf` — call guards where the return value needs no GIL work, scoped `nb::gil_scoped_release` inside `step` (it bumps `step_id`) and `get_render_output` (it builds the pixel view). Verify: a test with a concurrent Python worker thread shows thread progress during stepping.
- [x] 4.5 `Actuator` trampoline: Python subclasses registerable via `AddActuator`, `ComputeTorque` invoked per step with (angle, angular_velocity). Verify: test where a Python PD subclass drives a hinge joint, executed while `step` holds no GIL (proves callback GIL handling).
- [x] 4.6 Expose `SetJointTorque` in the binding surface. Verify: the Python-side torque tests from 6.x pass.

## 5. Wrapper package

- [x] 5.1 Create the `app/python/` package skeleton with `pyproject.toml` (scikit-build-core backend, project `needrename-sim`, cp313/win_amd64 metadata) and `src/needrename_sim/`. Verify: `python -m build --wheel` config resolves (build may still fail until task 8 wiring completes).
- [x] 5.2 Implement `__init__.py` discovery: `NEEDRENAME_RUNTIME_DIR`→bundled bin, `NEEDRENAME_DATA_ROOT`→bundled data root, `os.add_dll_directory` before importing `_physicsapp`, actionable failure, no once-only import guards. Verify: import tests (env-var path, bundled path, failure message, spawn-subprocess re-import) pass.
- [x] 5.3 Implement `robot.py`: name→id lookup over URDF results, batch `set_target_angles`/`set_joint_torques`, joint states as numpy arrays, snapshot-based `reset` (restore body fields, zero external force/torque, restore targets). Verify: reset test asserts bodies return to the initial snapshot and torque/force channels are zero.

## 6. Python tests

- [x] 6.1 Add `tests/data/minimal_robot.urdf` (hand-written, no meshes: base + 2 links, hinge joints, inertials) and URDF-import tests in physics-only mode (link/joint name maps, joint states, limits data). Verify: suite runs with only the wheel's data available.
- [x] 6.2 Complete the pytest suite: discovery/env-var errors, phase errors, zero-copy semantics, GIL thread progress, Python actuator subclass, `SetJointTorque` behavior, `robot.reset`. Verify: `pytest app/python/tests` passes from a clean venv install of the built wheel.

## 7. Examples

- [x] 7.1 `examples/windowed_robot.py <robot.urdf>`: windowed app, URDF import, actuators with default targets, camera pose, main loop with `render_next_frame`. Verify: runs interactively and closes cleanly on window close.
- [x] 7.2 `examples/headless_render.py <robot.urdf>`: physics-only stepping plus offscreen render readback; saves/inspects numpy frames from Python. Verify: produces frame images with no window created.
- [x] 7.3 `examples/rl_loop_sketch.py <robot.urdf>`: snapshot/reset, decimated control ticks, simple Python policy driving batch targets or torques. Verify: runs N episodes headless and logs per-episode totals.

## 8. Wheel packaging

- [x] 8.1 Wire scikit-build-core to build the engine + extension and stage DLLs (PhysicsApp, Engine*, SDL3, third-party runtime) plus minimal data (physics SPIR-V, `builtin_assets/`, trimmed default project) into `needrename_sim-*-cp313-win_amd64.whl`. Verify: wheel builds via `python -m build`; contents contain no robot assets and no cubemap textures.
- [x] 8.2 Clean-machine smoke: fresh Python 3.13 venv, `pip install` the wheel (no engine build env present), run the physics-only smoke and one example with a user-supplied URDF. Verify: import resolves via bundled locations without env vars; smoke passes.
- [x] 8.3 Write the package README: build-machine requirements (VS2026, Vulkan SDK, SDL3, parser Python deps), `NEEDRENAME_RUNTIME_DIR`/`NEEDRENAME_DATA_ROOT` semantics, consumer requirements (Python 3.13 + GPU driver), usage snippets. Verify: every documented command runs as written.

## 9. Review follow-up: the bindings are an application, not a top-level tree

- [x] 9.1 Move the nanobind sources and the `needrename_sim` distribution project into `app/python/`, and configure the extension from `app/CMakeLists.txt` after the `PhysicsApp` target (guarded by `ENGINE_BUILD_PYTHON_BINDINGS`; the root warns when the option is on while `ENGINE_BUILD_APPS` is off). Verify: `cmake --preset msvc` + `cmake --build --preset msvc-debug --target _physicsapp` produce `build/msvc/python/needrename_sim/_physicsapp*.pyd`, and `pytest app/python/tests` reports 13 passed against that tree.
- [x] 9.2 Give the wheel build its own persistent tree under the repository build root (`build/python/{wheel_tag}`) instead of one inside the package sources, and update the package README, the test configuration's repository-root resolution (`tests/conftest.py`), and the documented commands. Verify: `python -m build --wheel --outdir build/wheel app/python` succeeds in `build/python/cp313-cp313-win_amd64` (6.62 MB wheel, 114 entries, no cubemap or robot assets), the wheel installs into a clean venv, and `pytest app/python/tests` there reports 13 passed.
