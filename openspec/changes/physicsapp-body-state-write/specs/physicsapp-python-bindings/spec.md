## MODIFIED Requirements

### Requirement: Robot convenience layer and episode reset
The package SHALL provide a robot-level helper over a URDF import result:
joint/link name lookup, batch target-angle and joint-torque setting (by name or
id), joint states as numpy arrays, and a `reset` that restores the scene to the
state recorded at `commit_scene` (body poses and velocities, zeroed external
forces and torques, restored actuator targets) by delegating to the app-level
episode reset. The helper SHALL NOT keep its own state snapshot and SHALL NOT
require a recording call before `reset`. Consumers bring their own URDF; robot
assets are not bundled.

#### Scenario: Policy loop over named joints
- **WHEN** a Python policy reads joint states by name and sets batch target
  angles each control tick
- **THEN** the helper resolves names to ids and applies all values without
  per-joint boilerplate

#### Scenario: Reset restores the initial episode state
- **WHEN** `reset` is called after a simulation episode
- **THEN** all body positions and rotations match the state recorded at
  `commit_scene`, the recorded velocities are written back as integration seeds,
  external forces and torques are zero, and actuator targets are back at their
  commit-time values
- **AND** the following step derives velocity from the pose change (XPBD), so a
  restored velocity is the input to that derivation rather than a state that
  survives the step unchanged

#### Scenario: Reset works without a snapshot call
- **WHEN** a helper is created over a URDF import result, the episode runs, and
  `reset` is called without any prior recording call
- **THEN** the scene is restored to the commit-time state and the readback
  reflects it immediately
