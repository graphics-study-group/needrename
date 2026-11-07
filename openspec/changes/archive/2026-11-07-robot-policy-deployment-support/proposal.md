# Robot Policy Deployment Support

## Why

Deploying a robot RL policy (Unitree A1) through the `needrename_sim` Python package exposed three physics-layer gaps: the engine's fixed 1/60 s step cannot match a policy's training rate (200 Hz physics / 50 Hz control), actuator torques on serial joint chains overwrite each other on shared bodies so multi-link legs cannot hold or move their joints, and small-but-valid inertia tensors (the A1 foot: 0.06 kg, `diag(9.6e-6)`, determinant 8.9e-16) fall below an absolute determinant epsilon and get zeroed inverse inertia, freezing the welded lower leg. An earlier ad-hoc worktree patched these problems by leaking app-level knobs into engine Core/Framework state and adding cross-step slot bookkeeping to the actuator path, which breaks layering and the write-channel semantics. This change implements the three capabilities with a coherent architecture.

## What Changes

- **BREAKING** — Write-channel round semantics: every write (`SetBodyValue`, joint torque) takes effect for one round (until the next `Step` consumes it). Position/rotation/velocity persistence emerges from the state itself (the post-step readback refreshes the staging baseline); external force/torque are per-step quantities that the solver consumes and clears after each step (cleared after the last substep; a paused step defers consumption to the next step that actually integrates).
- **BREAKING** — `SetJointTorque` becomes `AddJointTorque` with accumulate (`+=`) semantics; multiple joints sharing a body compose by call order, body-level `SetBodyValue(ExternalTorque)` remains a direct assignment that overwrites whatever landed on that slot earlier in the round. The caller manages write order.
- Actuator torques accumulate across joints on shared bodies (a serial chain's middle link receives the sum of both joints' equal-and-opposite contributions), fixing multi-link leg control.
- `CreateInfo` gains `physics_time_step` / `physics_substeps` / `physics_iterations` (0 keeps the engine default). The knobs are forwarded into the solver's `XpbdConfig` at creation time through a new optional config parameter on `MainClass::LoadProject`; no physics fields enter `StartupOptions` or `MainClass` state.
- Inertia invertibility becomes a scale-relative test (`|det|` vs machine epsilon times Frobenius-norm cubed, computed in double) plus a positive-definiteness check; a singular tensor still gets zero inverse inertia, while an invalid (negative-definite) manual tensor throws `std::invalid_argument` instead of silently producing garbage dynamics.
- The uncommitted ad-hoc worktree changes are superseded; their regression tests (chained-joint scenario, tiny-inertia fixture) are carried over into this change.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `physics-app-body-write`: write lifetime changes from "persistent until overwritten" to round-based (one effect per step); solver-side force/torque clearing replaces the "SHALL NOT be cleared by the solver" contract; the readback refresh becomes the staging baseline for all six fields.
- `physicsapp-joint-torque`: `SetJointTorque` renamed to `AddJointTorque` with accumulate semantics and round lifetime; joint/body write interaction resolves by call order.
- `physicsapp-joint-actuators`: actuator torques accumulate on shared bodies instead of last-writer-wins, fixing serial chains.
- `physics-solver-interface`: external force/torque buffers become per-step quantities consumed (cleared) by the step; clearing aggregated at `PhysicsSystem::GPUStep` when multiple solvers share a scene.
- `physics-app`: `CreateInfo` gains the physics timing knobs; `Step` advances by the configured time step instead of a hardcoded 1/60.
- `physics-main-loop-integration`: `LoadProject` accepts an optional `XpbdConfig` override instead of always registering the solver with hardcoded defaults.
- `physicsapp-episode-reset`: `ResetToInitial` additionally discards uncommitted round writes (pending force/torque and state overrides do not survive a reset).
- `physicsapp-python-bindings`: `set_joint_torque` renamed to `add_joint_torque`; `CreateInfo` exposes the physics timing knobs.
- `shape-inertia-functions`: an inertia invertibility gate (scale-relative determinant test) governs when `ComInertiaComputer` inverts a tensor.
- `manual-inertia-tensor`: the manual path runs the same invertibility gate and throws `std::invalid_argument` on an invalid (non-positive-definite) manual tensor.

## Impact

- **Code**: `app/physics/PhysicsApp.{h,cpp}` (round semantics, `AddJointTorque`, actuator accumulation), `app/python/*` (binding rename, `CreateInfo` fields), `engine/Physics/Solver/*` (`XpbdGpuSolver`, `DummySolver`, `PhysicsSystem` consume-and-clear), `engine/Framework/MainClass.{h,cpp}` (`LoadProject` signature), `engine/Framework/Bridge/Internal/ComInertiaComputer.hpp` (invertibility gate), `engine/Core/Functional/OptionHandler.h` (ad-hoc fields reverted).
- **BREAKING for Python users**: `set_joint_torque` → `add_joint_torque`; force/torque writes no longer persist across steps (re-apply each round); existing scenarios in `test_joint_torque.py` and `test_robot.py` are rewritten to round semantics.
- **Engine users**: `RigidBodyComponent::m_external_force` / `m_external_torque` become one-shot initial values (consumed by the first step) instead of persistent fields; no in-tree consumer relies on the old behavior.
- **Tests**: new regression coverage for chained-joint torque accumulation and small-inertia rotation; C++ mirror (`physics_app_joint_torque_test.cpp`) updated alongside the Python suite.
