## MODIFIED Requirements

### Requirement: Python simulation surface
The distribution SHALL provide a `needrename_sim` package whose importable
surface wraps the `PhysicsApp` consumer API: create (execution mode, resolution,
title, background color), scene building (box/sphere/cylinder bodies, fixed and
hinge joints, actuator registration/retrieval/removal, URDF import returning
link/joint name maps), `commit_scene`, `step`, `render_next_frame`,
pause/resume/quit checks, per-body state readback and writes, joint
state/limits/targets, joint torque, actuator parameter tuning, and render
output. Wrong-phase usage and invalid handles SHALL raise Python exceptions
translated from the C++ errors (phase misuse and invalid handles
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

## ADDED Requirements

### Requirement: Actuator instances are shared between Python and the app
Registering an actuator SHALL share it between Python and the app: the object
passed to `add_actuator` (a bound C++ actuator type or a Python subclass of the
exposed base) SHALL be the object the app runs, and `get_actuator(joint)` SHALL
return that same Python object. Parameters of the shipped actuator types SHALL
be readable and writable properties whose changes affect subsequent simulation
steps. `remove_actuator(joint)` SHALL detach the actuator and hand the joint's
torque channel back to manual control. Actuator registration, retrieval, and
removal SHALL be legal in both the pre-commit and post-commit phases.

#### Scenario: Registered actuator is the caller's object
- **WHEN** a Python actuator instance is registered with `add_actuator` and the
  same joint is queried with `get_actuator`
- **THEN** the returned object is the registered instance (identity preserved)
  and remains alive while registered even if the caller drops its reference

#### Scenario: Live parameter tuning
- **WHEN** a parameter of a registered actuator (for example `kp`) is changed
  from Python and the simulation is stepped
- **THEN** the next torque computation uses the new value

#### Scenario: Removal hands control back to the caller
- **WHEN** `remove_actuator` is called on a joint whose actuator was driving it
- **THEN** the joint is free for manual joint torque again and the removed
  instance stays usable by Python
