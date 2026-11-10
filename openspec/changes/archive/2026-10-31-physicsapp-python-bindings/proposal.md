# Proposal: PhysicsApp Python Bindings

## Why

`PhysicsApp` was designed as a distributable simulation entry point whose explicit
goal includes "a clean path toward future C API / Python distribution"
(`add-physics-app` design), and later changes deferred batch I/O "until the
Python GPU-array exposure work starts" (`physicsapp-body-state-write` design).
That work starts now: users want to run the simulator, load robots, and drive
them with Python control policies (RL training and interactive visualization),
which today requires C++ test executables.

## What Changes

- **New** nanobind-based Python binding module for `PhysicsApp` (and the
  `Actuator` hierarchy with trampoline support so Python can subclass
  `Actuator`), built as `needrename_sim._physicsapp`:
  - Readback APIs exposed as **zero-copy numpy views** over the CPU readback
    arrays (`GetBodyStates` SoA arrays, `GetRenderOutput` pixels); GPU-resident
    state is never exposed directly — it keeps flowing through the existing
    per-step CPU readback. Batch/atomless returns (URDF name maps, small
    structs) copy as usual.
  - Long-blocking calls (`Step`, `RenderNextFrame`, `GetRenderOutput`,
    `LoadUrdf`) release the GIL via `nb::call_guard`.
- **New** `PhysicsApp::SetJointTorque(JointId, float)` manual torque channel:
  caller-managed joint torque applied as equal-and-opposite world-space torques
  about the joint axis (axis resolved internally). Throws `std::logic_error`
  when the joint has an actuator (the actuator owns the torque channel).
- **New** `needrename-sim` Python distribution package (wheel, `cp313-win_amd64`
  first): an external wrapper package whose `__init__.py` resolves and loads
  engine DLLs via the `NEEDRENAME_RUNTIME_DIR` / `NEEDRENAME_DATA_ROOT`
  environment variables (wheel-bundled location as fallback) before importing
  the extension, plus a Python convenience layer (`robot.py`: name→id maps,
  batch targets/torques, snapshot-based reset), pytest suites, and examples
  (windowed, headless-offscreen, RL-loop sketch).
- **New** `third_party/nanobind` git submodule; an `app/python/` build target
  driven by the same user-provided `Python3_EXECUTABLE` as the reflection
  parser.
- **BREAKING** (visual/data): the default project ships **without** the skybox
  cubemap texture. The window background becomes a solid clear color configured
  through a new `CreateInfo` background-color field, plumbed into
  `ComplexRenderGraphBuilder::BuildDefaultRenderGraph`. Wheel data shrinks to
  ~0.2 MB (physics SPIR-V + builtin_assets + trimmed default project).
- Robot assets (e.g. A1) are **not** shipped in the wheel; Python consumers
  bring their own URDF. Tests use a tiny hand-written URDF fixture (physics-only
  import needs no meshes); examples take a URDF path argument.

## Capabilities

### New Capabilities
- `physicsapp-python-bindings`: nanobind module surface for `PhysicsApp`/`Actuator`, zero-copy numpy readback semantics, GIL release rules, `needrename-sim` wrapper package with environment-variable DLL/data discovery, wheel packaging, Python tests and examples.
- `physicsapp-joint-torque`: `SetJointTorque` manual joint-torque channel on `PhysicsApp` — semantics, phase rules, ownership vs actuators.

### Modified Capabilities
- `physics-app`: `CreateInfo` carries a background clear color; the default project loads without skybox texture and the background renders as that solid color (replacing the skybox-material requirement wording and the `empty_with_sky`-only path).

## Impact

- **Code**: `app/physics/PhysicsApp.{h,cpp}` (SetJointTorque, CreateInfo field,
  joint axis already recorded per joint), `app/physics/Actuator.{h,cpp}`
  unchanged, `engine/Framework/Tools/ComplexRenderGraphBuilder.{h,cpp}`
  (clear-color parameter), `projects/empty_with_sky/` (drop skybox reference +
  `environment/`), new `app/python/` (nanobind sources + CMake, plus the
  `pyproject.toml` distribution project, `needrename_sim/`, `tests/` and
  `examples/`).
- **Dependencies**: `third_party/nanobind` submodule (new); Python 3.13
  (`cp313`) for the first wheel; scikit-build-core as build backend.
- **Build**: wheel build reuses the existing install rules and requires the
  full engine build environment on the build machine (VS2026, Vulkan SDK,
  SDL3, parser Python deps); consumers need only Python + GPU driver.
- **Not affected**: engine physics solver, editor, reflection system, C API
  (still future work; the binding links C++ directly).
