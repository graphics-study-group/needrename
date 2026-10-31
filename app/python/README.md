# needrename-sim

Python bindings for the `PhysicsApp` GPU physics simulator: build scenes, load
URDF robots, and drive them from Python with zero-copy numpy readback — for
reinforcement-learning training loops and interactive visualization.

```python
import needrename_sim as sim

app = sim.PhysicsApp.create(sim.CreateInfo(mode=sim.AppMode.physics_only))
app.add_box(sim.BoxDesc(position=sim.Vec3(0, 0, 1.0), half_extents=sim.Vec3(0.5, 0.5, 0.5)))
app.commit_scene()

for _ in range(100):
    app.step()

states = app.get_body_states()   # zero-copy numpy views: (n, 4) float32 each
print(states.positions)
```

## Requirements

**Consumers** (machines that install the wheel) need:

- CPython **3.13** on Windows x64 (first wheel target; other versions need their
  own build — the extension is not stable-ABI);
- a **GPU driver with Vulkan** support. The Vulkan loader ships with the driver
  and is not bundled; SDL3 and the engine libraries are.

No C++ toolchain, Vulkan SDK, or engine checkout is required to use the wheel.

**Building** the wheel from source needs the full engine build environment:

- Visual Studio 2026 (or 2022) with *Desktop development with C++*;
- Vulkan SDK 1.4.x (`VULKAN_SDK` set);
- SDL3 development package (`SDL3_ROOT` set — see
  `docs/build_instructions/windows_msvc.md`);
- CMake ≥ 3.21 and the Visual Studio generator;
- a Python 3.13 interpreter with the reflection parser requirements
  (`clang`, `mako`): the repository `.venv` created by
  `docs/build_instructions/windows_msvc.md` is exactly this;
- the git submodules (`git submodule update --init --recursive`), including
  `third_party/nanobind`.

## Development iteration (no wheel)

The extension is part of the engine build. With `ENGINE_BUILD_PYTHON_BINDINGS`
on (the default), `cmake --build --preset msvc-debug` compiles `_physicsapp` into
the project's normal per-configuration output directory,
`build/msvc/bin/Debug/` (Release lands in `.../Release/`), next to the engine
DLLs it links — so the two configurations never overwrite each other and a
configuration can never mix with the other one's DLLs. A development run imports
the package sources straight from `app/python/src`; the package then picks the
extension up from beside the engine DLLs. Nothing is staged or copied next to
the sources. scikit-build-core runs only when packaging a wheel, so this path
reuses the CMake build tree you already have.

```powershell
cmake --build --preset msvc-debug
.venv\Scripts\python -m pytest app\python\tests
```

The test configuration locates the built configuration directory (Debug first,
then RelWithDebInfo/Release) and a developer data root (`build\py-runtime` or
`install`) on its own; use the environment variables below when running scripts
by hand.

## Building the wheel

From the repository root, with the environment variables above set:

```powershell
# Once: build tooling and the reflection parser interpreter (the repo .venv).
.venv\Scripts\python -m pip install -r third_party/AnnoRefl/parser/requirements.txt
.venv\Scripts\python -m pip install build scikit-build-core numpy pytest

# Build the wheel (the engine is configured and built by scikit-build-core,
# in its own persistent tree under build/python/<wheel tag>).
.venv\Scripts\python -m build --wheel --outdir build\wheel app\python
```

The wheel lands in `build\wheel\` as
`needrename_sim-0.1.0-cp313-cp313-win_amd64.whl`. It contains the engine shared
libraries, the compiled extension, the minimal runtime data (physics SPIR-V,
built-in assets, the default project) and the Python sources — no robot assets,
no sample content, no documentation images.

The build is a "build on the development machine, distribute binaries" flow:
it reuses the engine's own CMake configuration and install rules, so the wheel
needs the same environment as any engine build.

## Installing and running

```powershell
python -m venv .venv-app
.venv-app\Scripts\python -m pip install build\wheel\needrename_sim-0.1.0-cp313-cp313-win_amd64.whl
.venv-app\Scripts\python -c "import needrename_sim; print(needrename_sim.__version__)"
```

Installed wheels resolve their bundled libraries and data automatically.

### Runtime discovery and the environment variables

On import the package locates three things:

| What | Environment variable | Fallback |
| --- | --- | --- |
| Engine DLLs (`PhysicsApp.dll`, `Engine*.dll`, `SDL3.dll`, …) | `NEEDRENAME_RUNTIME_DIR` | `needrename_sim/bin/` |
| Runtime data root (`builtin_assets/`, `projects/`, `spirv/`) | `NEEDRENAME_DATA_ROOT` | `needrename_sim/share/needrename/` |
| Compiled extension (`_physicsapp*.pyd`) | `NEEDRENAME_BINDINGS_DIR` (override only) | the package directory, then beside the engine DLLs, then `<data root>/python/` |

- The environment variables take precedence and are meant for development:
  point them at an engine build's `bin/Debug` and an installed data root
  (`<prefix>/share/needrename`) to run against a locally built engine.
- The extension needs no variable in the two common layouts: an installed wheel
  bundles it inside the package, and a build tree links it next to the engine
  DLLs. `NEEDRENAME_BINDINGS_DIR` exists for layouts that move it elsewhere.
- On Windows the DLL search directories are registered before the extension is
  imported; `PATH` is not used.
- Import fails with a message naming the variables that did not resolve (the
  runtime/data pair, and the extension location when it is missing).
- Import has no once-only side effects, so spawned `multiprocessing` workers
  (the recommended way to run parallel environments) can import the package and
  create their own app.

```powershell
# Development run against a build tree: sources + bin/Debug (DLLs and extension).
$env:PYTHONPATH = "$PWD\app\python\src"
$env:NEEDRENAME_RUNTIME_DIR = "$PWD\build\msvc\bin\Debug"
$env:NEEDRENAME_DATA_ROOT = "$PWD\build\py-runtime\share\needrename"
.venv\Scripts\python -c "import needrename_sim"
```

## API notes

- **One app per process.** The engine's `MainClass` is a process singleton, so
  create exactly one `PhysicsApp` per process. Parallelism is process-level:
  use one worker process per environment.
- **Lifecycle.** `PhysicsApp.create(...)` → building calls (`add_box`,
  `load_urdf`, joint/light/camera setup) → `commit_scene()` → drive phase
  (`step`, `render_next_frame`, readback). Wrong-phase calls raise
  `RuntimeError`; invalid handles raise `IndexError`.
- **Zero-copy readback.** `get_body_states()` and `get_render_output()` return
  numpy arrays that view the engine's CPU readback memory — no copy. Their
  contents are valid until the next `step()` / frame capture; `step_id` on the
  body-state view detects staleness, and `.copy()` takes a snapshot. A live view
  keeps its producing app alive.
- **GIL.** `step`, `render_next_frame`, `get_render_output` and `load_urdf`
  release the GIL while they block, so other Python threads keep running. A
  Python actuator's `compute_torque` (and `reset`) is called with the GIL held:
  the trampoline acquires it around the callback.
- **Actuators.** `add_actuator(joint, actuator)` shares ownership: the object you
  pass is the object the app runs (`get_actuator(joint) is actuator`), it stays
  alive while registered even if you drop your reference, and registration works
  both before and after `commit_scene`. The shipped types (`PdActuator`,
  `DcMotorActuator`) expose every constructor parameter as a read/write property
  (`kd`, `kp`, `stall_torque`, `no_load_speed`, `cont_torque`, `gear_ratio`), so
  gains can be tuned live between steps. Derive from `Actuator` and override
  `compute_torque` for a custom law; a stateful law also overrides `reset()` to
  clear its state. `ComputeTorque` is not const, so per-step state is allowed.
- **Torque channels.** `add_actuator(joint, actuator)` gives an actuator
  ownership of a joint's torque. Joints without an actuator can be driven with
  `set_joint_torque(joint, torque)`; both write the same persistent per-body
  external-torque channel (last write wins, cleared by `Robot.reset()`).
  `remove_actuator(joint)` detaches the actuator, clears that channel (the last
  applied actuator torque stops applying), and hands the joint back to
  `set_joint_torque`; the removed instance stays usable from Python.
- **Releasing the app.** The engine holds GPU resources: destroy the app (for
  example by letting the function that created it return) before the
  interpreter shuts down. Scripts that keep the app in a module global until
  exit produce nanobind leak reports at shutdown.

## Robot helper

`needrename_sim.Robot` wraps a URDF import result with name-based access and
episode reset:

```python
result = app.load_urdf(sim.UrdfImportConfig(urdf_path="robot.urdf", position=sim.Vec3(0, 0, 1.0)))
for joint in result.joint_bodies.values():
    app.add_actuator(joint.id, sim.DcMotorActuator())  # A1 joint-side parameters

robot = sim.Robot(app, result)
robot.set_target_angles({name: 0.0 for name in robot.joint_names})
app.commit_scene()

# Live tuning: the registered instance is reachable from both sides.
first_joint = robot.joint_ids[robot.joint_names[0]]
app.get_actuator(first_joint).kp = 40.0

robot.snapshot()                 # episode start (after commit_scene, before stepping)
for _ in range(600):
    app.step()
robot.reset()                    # restores the snapshot and zeroes force/torque channels
```

`reset()` restores the body poses, linear/angular velocities and the target
angles this helper applied, and zeroes every body's external force and torque.
Targets set directly through `app.set_target_angle` are outside its bookkeeping.

## Tests and examples

```powershell
# Test suite (from the repository root, wheel installed or build tree present).
.venv\Scripts\python -m pytest app\python\tests

# Examples; each takes a URDF path (no robot assets are bundled). `python` below
# is an interpreter that has needrename-sim installed (a wheel install such as
# .venv-app, or the repository .venv with the dev-mode variables above).
python app\python\examples\windowed_robot.py <robot.urdf>
python app\python\examples\headless_render.py <robot.urdf> --frames 8 --out frames
python app\python\examples\rl_loop_sketch.py <robot.urdf> --episodes 3 --steps 120
```

`headless_render.py` writes PPM frames; `rl_loop_sketch.py` runs headless
episodes and logs per-episode totals. The tests use a hand-written,
mesh-free URDF fixture (`app/python/tests/data/minimal_robot.urdf`), so they pass
with only the wheel's data available.
