# physicsapp-python-bindings

## Purpose

Expose the `PhysicsApp` simulator to Python through a distributable `needrename_sim` package so control policies can build scenes, run robots, and drive them from Python (RL training and interactive visualization), with zero-copy numpy readback and no engine-internals knowledge required.

## Requirements

### Requirement: Python simulation surface
The distribution SHALL provide a `needrename_sim` package whose importable
surface wraps the `PhysicsApp` consumer API: create (execution mode, resolution,
title, background color), scene building (box/sphere/cylinder bodies, fixed and
hinge joints, actuator registration, URDF import returning link/joint name
maps), `commit_scene`, `step`, `render_next_frame`, pause/resume/quit checks,
per-body state readback and writes, joint state/limits/targets, joint torque,
and render output. Wrong-phase usage and invalid handles SHALL raise Python
exceptions translated from the C++ errors (phase misuse and invalid handles
distinguishable from initialization failure).

#### Scenario: Headless run loop from Python
- **WHEN** a Python script creates the app in physics-only mode, builds a scene,
  commits, and runs a `step` loop with joint-state reads
- **THEN** the simulation advances without any window or engine-internal calls

#### Scenario: Windowed run from Python
- **WHEN** a Python script creates the app in windowed mode and calls
  `render_next_frame` each loop iteration
- **THEN** the engine window opens and stays responsive (event processing
  happens inside the call) until `should_quit` reports window close

#### Scenario: Wrong-phase call raises
- **WHEN** Python calls `step` before `commit_scene`, or a scene-building call
  after `commit_scene`
- **THEN** a Python exception is raised identifying the phase violation

### Requirement: Python-defined actuators
Python classes deriving from the exposed actuator base SHALL be registerable on
joints through the actuator-registration API. Each simulation step SHALL invoke
the Python subclass's torque computation with the measured joint angle and
angular velocity and apply the returned torque, with the GIL held during the
callback.

#### Scenario: Python actuator drives a joint
- **WHEN** a Python subclass implementing the torque computation is registered
  on a hinge joint and the simulation is stepped
- **THEN** the subclass callback is invoked each step and its returned torque
  influences the joint's motion

### Requirement: Zero-copy numpy readback
Batch body-state arrays (positions, rotations, linear velocities, angular
velocities, slot mapping) and rendered pixels SHALL be exposed as numpy arrays
that view the engine's CPU readback memory without copying. GPU-resident
simulation state SHALL be exposed only through the existing per-step CPU
readback; the package SHALL NOT expose GPU memory or require GPU interop.

#### Scenario: Readback arrays are views, not copies
- **WHEN** Python obtains the batch body-state arrays and the engine steps once
  afterwards
- **THEN** the previously obtained arrays reflect the new values in place
  (shared memory, no copy was made)

#### Scenario: Render output is a zero-copy pixel view
- **WHEN** render readback is enabled and Python obtains the rendered frame
- **THEN** the returned numpy array has shape (height, width, 4) uint8 and
  views the engine staging memory without copying

#### Scenario: Views stay memory-safe while their app is reachable
- **WHEN** a returned view is still referenced by Python after further steps
- **THEN** no use-after-free occurs: the view keeps its producing app alive,
  and its documented content validity (until the next producing call) is the
  only limit

### Requirement: Blocking calls release the GIL
Calls that block on the GPU or on file parsing (`step`, `render_next_frame`,
render-output retrieval, URDF import) SHALL release the Python GIL for their
duration so other Python threads can run concurrently.

#### Scenario: Python threads progress during a step
- **WHEN** one thread calls `step` in a loop while another thread executes
  Python work
- **THEN** the second thread makes progress while steps are executing

### Requirement: DLL and data discovery via environment variables
On import the package SHALL locate the engine shared libraries and the runtime
data root using, in order: the `NEEDRENAME_RUNTIME_DIR` and
`NEEDRENAME_DATA_ROOT` environment variables, then locations bundled inside the
wheel. On Windows the DLL search directories SHALL be registered before the
extension module is imported. When neither resolves, import SHALL fail with an
error naming both variables. Import SHALL be repeatable in spawned
subprocesses (no once-only side effects), so multiprocessing-based parallel
environments work.

#### Scenario: Environment variables drive discovery
- **WHEN** `NEEDRENAME_RUNTIME_DIR` and `NEEDRENAME_DATA_ROOT` point at an
  engine build's bin directory and data root
- **THEN** importing `needrename_sim` succeeds and the app loads its assets
  from the configured data root

#### Scenario: Missing locations fail with actionable error
- **WHEN** neither the environment variables nor a bundled location resolve
- **THEN** import raises an error that names `NEEDRENAME_RUNTIME_DIR` and
  `NEEDRENAME_DATA_ROOT`

#### Scenario: Subprocess import works
- **WHEN** a multiprocessing worker process (spawn start method) imports
  `needrename_sim` and creates an app
- **THEN** discovery succeeds exactly as in the parent process

### Requirement: Wheel distribution with minimal data
The build SHALL produce a binary wheel (first target: CPython 3.13, Windows
x64) containing the binding extension, the runtime shared libraries, and the
minimal runtime data: physics SPIR-V shaders, builtin assets, and the default
project without skybox textures. The wheel SHALL NOT contain robot assets,
sample content, or documentation images. Consuming the wheel SHALL require only
Python and a GPU driver, not a C++ build environment.

#### Scenario: Wheel installs and runs standalone
- **WHEN** the wheel is installed into a Python 3.13 environment on a machine
  without the engine build environment
- **THEN** creating an app in physics-only mode succeeds using bundled
  libraries and data

#### Scenario: Wheel data stays minimal
- **WHEN** the wheel contents are inspected
- **THEN** no robot description assets and no cubemap skybox textures are
  present

### Requirement: Robot convenience layer and episode reset
The package SHALL provide a robot-level helper over a URDF import result:
joint/link name lookup, batch target-angle and joint-torque setting (by name or
id), joint states as numpy arrays, and a snapshot-based `reset` that restores
all body states to the recorded initial snapshot, zeroes external forces and
torques, and restores actuator targets. Consumers bring their own URDF; robot
assets are not bundled.

#### Scenario: Policy loop over named joints
- **WHEN** a Python policy reads joint states by name and sets batch target
  angles each control tick
- **THEN** the helper resolves names to ids and applies all values without
  per-joint boilerplate

#### Scenario: Reset restores the initial episode state
- **WHEN** `reset` is called after a simulation episode
- **THEN** all body positions and rotations match the initial snapshot, the
  recorded velocities are written back as integration seeds, external forces and
  torques are zero, and actuator targets are back at their initial values
- **AND** the following step derives velocity from the pose change (XPBD), so a
  restored velocity is the input to that derivation rather than a state that
  survives the step unchanged

### Requirement: Python tests and examples
The distribution SHALL ship a pytest suite covering the binding (import
discovery, phase errors, zero-copy semantics, joint torque, reset, actuator
subclassing) that runs without bundled robot assets, using a minimal
hand-written URDF fixture (no meshes; physics-only import). It SHALL ship
examples: a windowed robot demo taking a caller-supplied URDF path, a
headless/offscreen example that renders without a window and consumes pixels
from Python, and an RL-style loop sketch (reset, decimated control ticks).

#### Scenario: Test suite runs without robot assets
- **WHEN** the pytest suite runs on a machine with only the wheel's data
- **THEN** all tests pass using the minimal URDF fixture

#### Scenario: Examples cover windowed and headless use
- **WHEN** the examples are run as documented
- **THEN** the windowed demo shows the caller's robot in an engine window and
  the headless example produces rendered frames as numpy pixel arrays
