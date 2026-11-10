# Design: PhysicsApp Python Bindings

## Context

See proposal.md for motivation. State that shapes this design:

- `app/physics/PhysicsApp.h` is already a binding-clean surface: glm/std types
  only, opaque `uint32_t` handles, factory + `unique_ptr`, exceptions documented
  at the DLL boundary, and an `Actuator` base class explicitly designed for
  external derivation. No Framework/World/Asset types leak into the header.
- All engine modules are SHARED libraries and `PhysicsApp` builds with
  `WINDOWS_EXPORT_ALL_SYMBOLS ON`, so a nanobind extension can link it directly.
- CMake already discovers a user-provided `Python3_EXECUTABLE` (repo `.venv`,
  verified Python 3.13.5) for the reflection parser; the binding reuses it.
- The installable-distribution layout (`bin/` + `share/needrename/` data root,
  `NEEDRENAME_DATA_ROOT` override, `cmake/install_rules.cmake`) is the shipping
  contract the wheel reuses.
- Runtime state exposure today: `GetBodyStates` (SoA `std::span` over per-step
  CPU readback, "valid until the next `Step`"), `GetRenderOutput` (host-visible
  staging pixels), and `SetBodyValue` (six writable fields, caller-managed
  external force/torque lifetime). GPU buffers are never exposed.
- `ComplexRenderGraphBuilder::BuildDefaultRenderGraph` clears HDR color with a
  default-constructed `ColorClearValue` (black); the skybox draws only when the
  level carries a skybox material (`SceneDataManager::DrawSkybox` early-outs).

## Goals / Non-Goals

**Goals:**
- One design that serves both consumer personas: RL training (headless, batch
  obs) and interactive visualization (windowed), from the same API.
- Zero-copy wherever the data already exists on the CPU; explicit copy only
  where a copy already happens anyway.
- Wheel consumable by a machine with only Python 3.13 + GPU driver.

**Non-Goals:**
- C API / stable ABI layer (still future work; the binding links C++ directly).
- CUDA/Vulkan interop or exposing GPU buffers to Python.
- Vectorized simulation inside one process (multi-scene rendering is a known
  engine limitation; parallelism is process-level).
- Python keyboard/mouse input query API (`pause`/`resume`, `set_camera_pose`,
  `should_quit` suffice for v1).
- Linux wheels and multi-version (cp31x) wheel matrices (layout must not
  preclude them; shipping them is a later change).

## Decisions

### D1: nanobind as the binding technology (submodule in `third_party/nanobind`)
Chosen over pybind11 (slower compiles, heavier) and over a C API + cffi route
(two boundaries to build, no direct numpy, no Python subclassing; the specs
already treat C API as a separate future step). nanobind fits the clean header
directly: `nb::class_` for `PhysicsApp`/`Actuator`, trampolines for
Python-defined actuators, `nb::ndarray` for zero-copy numpy. Acquired as a
`third_party/` git submodule per repo convention (AnnoRefl et al.), built with
the same `Python3_EXECUTABLE` the reflection parser uses.

### D2: Zero-copy via `nb::ndarray` views; GPU state stays behind the readback
- `GetBodyStates` SoA spans and `GetRenderOutput` pixels become numpy views
  (`nb::ndarray`) with **no copy**: `float32` arrays for positions/velocities,
  `uint32` for slot indices, `(H, W, 4) uint8` for pixels.
- The view's base/owner is the producing app's Python object, so a live numpy
  view keeps the app (and its readback memory) alive; app destruction cannot
  dangle under a view. Content validity stays as documented ("until the next
  `Step`" / next capture); the wrapper exposes a `step_id` on state views so
  staleness is detectable, and `.copy()` is the user's escape hatch.
- Everything else (URDF name maps, single-body/joint structs, limits) copies —
  these are small and semantically independent objects.
- GPU-resident data is never surfaced (no interop): the per-step CPU readback
  remains the only observation path, exactly the "on GPU → copy" boundary.
- Alternatives considered: copy-by-default with `copy=False` opt-in (rejected —
  user directive is zero-copy first); exposing mapped device memory (rejected —
  no CUDA/Vulkan interop in scope).

### D3: GIL released only around blocking calls
`nb::call_guard<nb::gil_scoped_release>` on `render_next_frame` and `load_urdf`
(file parse). `step` and `get_render_output` release the GIL with a scoped
`nb::gil_scoped_release` inside the function body instead of a call guard: both
must still run Python-visible work while holding the GIL (`step` bumps the state
views' `step_id`; `get_render_output` constructs the returned `nb::ndarray`
view), so the guard cannot outlive the blocking wait. Small per-item getters
keep the GIL (release/acquire overhead would dominate per-joint loops).

Python actuator callbacks run while `step` holds no GIL. The binding therefore
routes Python-defined actuators through a C++ `Actuator` adapter that owns the
Python object and re-acquires the GIL per callback; because
`Actuator::SetTargetAngle` is not virtual, the adapter also mirrors app-level
target writes onto the Python object (and keeps a shadow target so a target the
Python object set itself is not overwritten). A follow-up change replaces the
adapter with `std::shared_ptr` ownership plus an explicit
`nb::gil_scoped_acquire` inside the trampoline, which also removes the
`mutable` shadow field and the non-const-in-a-const-method workaround.

### D4: Exception translation
Register nanobind exception translators: `std::logic_error` → `RuntimeError`
(phase misuse, with message preserved), `std::invalid_argument` → `ValueError`,
`std::out_of_range` → `IndexError`, other `std::exception` → `RuntimeError`.
Python code can distinguish phase misuse from init failure by type + message.

### D5: `needrename_sim` wrapper package with env-var discovery
The bindings are an application of the engine, so the nanobind sources and the
distribution project share one directory, `app/python/`, configured from
`app/CMakeLists.txt` (after the `PhysicsApp` target, guarded by
`ENGINE_BUILD_PYTHON_BINDINGS`):

```
app/python/
├── CMakeLists.txt              nanobind target `_physicsapp` (dev staging + install rules)
├── bindings.cpp                module definition, exception translators
├── value_types.cpp             glm value types, enums, descriptors, state structs
├── physics_app_bindings.cpp    PhysicsApp, actuator trampoline, zero-copy views
├── binding_support.h           aggregate kwarg-construction helper
├── pyproject.toml              scikit-build-core backend, project needrename-sim
├── src/needrename_sim/
│   ├── __init__.py             discovery + DLL loading + re-export
│   └── robot.py                Robot helper: name maps, batch set, reset
├── tests/                      pytest suite + data/minimal_robot.urdf fixture
└── examples/                   windowed / headless-offscreen / rl-loop sketch
```

Import sequence in `__init__.py`:
1. Resolve runtime dir: `NEEDRENAME_RUNTIME_DIR` → wheel-bundled `bin/`;
   resolve data root: `NEEDRENAME_DATA_ROOT` → wheel-bundled `share/needrename`.
2. Windows: `os.add_dll_directory(runtime_dir)` **before** importing
   `_physicsapp` (Python 3.8+ ignores PATH for extension dependencies).
   Non-Windows: `LD_LIBRARY_PATH` note in docs (rpath set at build instead).
3. `from ._physicsapp import *`; failure to resolve raises an error naming both
   environment variables.
No once-only guards: spawn-based multiprocessing workers re-import cleanly and
inherit the variables.

### D6: Wheel = scikit-build-core, binaries + minimal data
- `[build-system]` scikit-build-core + nanobind; it drives CMake to build the
  engine and the extension, then stages `bin/` DLLs (PhysicsApp, Engine*,
  SDL3, third-party runtime) and the minimal data set through the existing
  install rules: physics SPIR-V, `builtin_assets/`, and the trimmed default
  project (**no** cubemap skybox, **no** robot assets, **no** sample content or
  docs images). Target tag: `cp313-win_amd64` first.
- Two build paths, and only one of them is a second tree. A developer build
  reuses the CMake preset tree: `ENGINE_BUILD_PYTHON_BINDINGS` is on by default,
  so `cmake --build --preset msvc-debug` compiles `_physicsapp` and stages it
  beside the package sources in `build/msvc/python/needrename_sim/`; nothing
  invokes scikit-build, and the tests resolve that directory themselves.
  Packaging is the only flow that configures its own tree, because it needs
  different engine cache variables (tests/docs off, `ENGINE_SHIPPED_PROJECTS=
  empty_with_sky`, `ENGINE_SHADER_MODE=spv`, and `SKBUILD=ON` selects the
  extension's in-package install destination). That tree is persistent and
  incremental at `build/python/{wheel_tag}`, next to `build/msvc`, never inside
  the package source directory.
- Consumer needs: Python 3.13 + GPU driver (the Vulkan loader ships with the
  driver; SDL3/PhysicsApp/Engine DLLs travel in the wheel).
- Build machine needs the full engine environment (VS2026, Vulkan SDK,
  SDL3 via `SDL3_ROOT`, parser Python deps `clang`/`mako`) — documented in the
  package README; this is a "build on the dev machine, distribute binaries"
  flow, not a from-source consumer flow.
- Alternative rejected: pure-setuptools backend shelling out to the repo
  presets (couples the wheel to checkout layout and presets); fat wheel with
  all sample data (violates the minimal-data directive).

### D7: `SetJointTorque` uses the existing external-torque write path
`void SetJointTorque(JointId, float)` (Drive phase): writes
`±torque * axis_world` into the two bodies' persistent external-torque fields
through the same `SetBodyValue` path actuators use; the joint's recorded world
axis is resolved internally (it is already computed per step for actuators).
Semantics: caller-managed persistence (like `ExternalForce`), last-writer-wins
against `SetBodyValue(ExternalTorque)` on the same body fields, and
`std::logic_error` when the joint has an actuator (actuator owns the channel —
manual torque mode = joints without actuators). No solver or engine change.

### D8: Background clear color plumbed through graph build; sky data trimmed
`CreateInfo` gains `background_color` (linear rgb, default black). The value is
passed into `ComplexRenderGraphBuilder::BuildDefaultRenderGraph` as the HDR
clear color when `CommitScene` builds the graph (graph is built once; no
runtime setter needed). The default project's level asset drops its
`m_skybox_material` reference and the `environment/` cubemap is deleted
(`empty_project`'s stale-schema level asset is not reused). Skybox rendering
itself is untouched — levels that reference a skybox (editor flows) still draw
one. Engine change is a parameter on the existing builder; the editor's call
site passes its current default.

### D9: Reset and vectorization live in the Python layer
Episode reset = snapshot at first `commit_scene` + replay via `SetBodyValue`
(six fields), zeroing external force/torque, restoring actuator targets.
No C++ batch-write API in this change (per-body calls are cheap at episode
granularity; batch SoA writes remain deferred until a real need appears).
Velocity writes are integration seeds, not authoritative state: XPBD derives
velocity from the pose delta in every substep, so "reset restores the recorded
velocity" holds only up to that derivation.

**Superseded by review.** The change's review found two defects in that path:
writes stay invisible to the measurement path (`GetBodyState`, `GetJointState`,
and the per-step actuator measurement read the previous step's readback) until a
`Step` runs, and a whole-field upload overwrites slots the caller never wrote
(the write staging holds zeros there). A follow-up change therefore moves the
episode reset into `PhysicsApp` — a `CommitScene` state snapshot plus
`ResetToInitial()` — and merges the readback and write staging so a write is
immediately visible and only the intended slot changes. The snapshot-based
replay described above is superseded there.

Parallel envs = multiprocessing workers, each with one `PhysicsApp` in
physics-only mode (worker processes are long-lived; `Create` cost is paid once).

## Risks / Trade-offs

- **[Zero-copy view semantics are subtle (in-place content mutation)]** →
  `step_id` on views + documented validity + tests for use-after-step and
  app-destruction safety; `.copy()` documented as the snapshot escape hatch.
- **[Python callbacks inside a GIL-released `Step` may mishandle the GIL]** →
  the adapter acquires the GIL per callback (D3), and a dedicated test runs a
  Python actuator while `step` holds no GIL. The remaining wart is the shadow
  target the non-virtual `SetTargetAngle` forces; the shared-ownership follow-up
  removes it.
- **[scikit-build-core must reproduce the CMake configuration the presets
  provide (VS generator, `SDL3_ROOT`, parser interpreter)]** → keep the
  forwarded cache variables minimal and listed in one place in
  `pyproject.toml`; if divergence hurts, the fallback is a small custom build
  step calling `cmake --preset` — no spec impact.
- **[Windows DLL search pitfalls (PATH ignored for extension deps)]** →
  `os.add_dll_directory` before import, env-var-first resolution, subprocess
  import test.
- **[Configured background color passes through bloom/tonemapping, so the
  presented pixel may differ from the configured value]** → the spec scenario
  asserts within tolerance; examples pick values that survive the pass.
- **[cp313-only first wheel; each Python version needs its own build]** →
  acceptable; multi-version/CI is an explicit follow-up.
- **[Build machine still needs the full toolchain (parser deps, Vulkan SDK)]**
  → documented; consumers are unaffected.
- **[Mixed dev/wheel DLLs (env var points at a dev build while a wheel is
  installed)]** → env-var precedence is deliberate for debugging; README
  documents that the runtime dir fully determines the loaded engine build.

## Migration Plan

1. Data trim first (default project loses skybox reference + `environment/`);
   visually this replaces the sky with the (default black) clear color —
   tree stays green because no test asserts sky pixels.
2. `CreateInfo.background_color` + builder parameter (default black keeps the
   current cleared look byte-identical for existing C++ tests).
3. `SetJointTorque` (purely additive).
4. Bindings, package, wheel (additive; nothing existing imports them).
Rollback: steps 1–3 are independently revertable; 4 is a new subtree. No
persisted-format changes, no data migration.

## Open Questions

- Linux wheel mechanics (`.so` bundling + rpath vs `LD_LIBRARY_PATH`) — the
  package layout is portable but Linux packaging is a later change.
- Publishing channel for wheels (internal index vs file drop) — does not
  affect build or API.
- Whether a future batch SoA write API (`reset` at C++ level) is needed for
  hot resets — **resolved by this change's review**: yes. The follow-up change
  adds a `CommitScene` snapshot with `ResetToInitial()` and merges the readback
  and write staging (see D9).
