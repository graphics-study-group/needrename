# physicsapp-python-bindings Delta

## MODIFIED Requirements

### Requirement: Python simulation surface

The distribution SHALL provide a `needrename_sim` package whose importable
surface wraps the `PhysicsApp` consumer API: create (execution mode, resolution,
title, background color, physics timing knobs `physics_time_step` /
`physics_substeps` / `physics_iterations`), scene building (box/sphere/cylinder
bodies, fixed and hinge joints, actuator registration/retrieval/removal, URDF
import returning link/joint name maps), `commit_scene`, `step`,
`render_next_frame`, pause/resume/quit checks, per-body state readback and
writes, joint state/limits/targets, joint torque accumulation
(`add_joint_torque`), actuator parameter tuning, and render output. Wrong-phase
usage and invalid handles SHALL raise Python exceptions translated from the C++
errors (phase misuse and invalid handles distinguishable from initialization
failure).

`add_joint_torque` SHALL have accumulate semantics matching the C++ API: values
compose within a round, take effect on the next `step`, and last exactly one
step (re-apply per step to keep a torque acting). There SHALL be no
persistent-torque `set_joint_torque` entry point.

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

#### Scenario: Physics rate configured from Python
- **WHEN** a Python script creates the app with
  `CreateInfo(physics_time_step=0.005)` for a 200 Hz policy deployment
- **THEN** each `step` advances 0.005 s, and a policy ticking every 4 steps
  runs at 50 Hz

#### Scenario: Manual torque re-applied per step
- **WHEN** a Python script calls `add_joint_torque` before every `step` in a
  loop with the same value
- **THEN** the torque acts on every step of the loop

### Requirement: Robot convenience layer and episode reset

The package SHALL provide a robot-level helper over a URDF import result:
joint/link name lookup, batch target-angle setting and batch joint-torque
accumulation (by name or id), joint states as numpy arrays, and a `reset` that
restores the scene to the state recorded at `commit_scene` (body poses and
velocities, zeroed external forces and torques, discarded pending writes,
restored actuator targets) by delegating to the app-level episode reset. The
helper SHALL NOT keep its own state snapshot and SHALL NOT require a recording
call before `reset`. Consumers bring their own URDF; robot assets are not
bundled.

#### Scenario: Policy loop over named joints
- **WHEN** a Python policy reads joint states by name and sets batch target
  angles each control tick
- **THEN** the helper resolves names to ids and applies all values without
  per-joint boilerplate

#### Scenario: Reset restores the initial episode state
- **WHEN** `reset` is called after a simulation episode
- **THEN** all body positions and rotations match the state recorded at
  `commit_scene`, the recorded velocities are written back as integration seeds,
  external forces and torques are zero, pending writes are discarded, and
  actuator targets are back at their commit-time values
- **AND** the following step derives velocity from the pose change (XPBD), so a
  restored velocity is the input to that derivation rather than a state that
  survives the step unchanged

#### Scenario: Reset works without a snapshot call
- **WHEN** a helper is created over a URDF import result, the episode runs, and
  `reset` is called without any prior recording call
- **THEN** the scene is restored to the commit-time state and the readback
  reflects it immediately
