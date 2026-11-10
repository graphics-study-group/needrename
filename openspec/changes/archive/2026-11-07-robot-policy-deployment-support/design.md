# Design: Robot Policy Deployment Support

## Context

See proposal.md — Why. Three requirements (configurable physics rate, serial-chain actuator composition, small-inertia correctness) surfaced while deploying an RL policy through the Python package. An earlier worktree implemented them ad hoc: physics knobs leaked through `StartupOptions` → `MainClass` members, and the actuator path gained cross-step slot bookkeeping (`actuated_slots`, `ReleaseActuatorSlots`) to manage a persistent torque channel. This design re-implements the same requirements with the write channel redefined as round-based, which eliminates the bookkeeping class of bugs instead of patching it.

Key existing facts that shape the approach:

- `Step()` is synchronous: it submits one command buffer and waits on a fence before returning ([PhysicsApp.cpp](app/physics/PhysicsApp.cpp)). User writes therefore always happen outside a step, between rounds.
- The upload path is whole-field per dirty field ([PhysicsApp.cpp:230](app/physics/PhysicsApp.cpp)): any slot write re-uploads the entire field. Stale values in a staging buffer are resurrection hazards.
- State channels (pos/rot/linvel/angvel) already have round semantics in practice: the post-step readback overwrites their staging, so a state write is consumed once and its effect persists only through the simulated state. Force/torque staging is write-only (`StagingToDevice`), never refreshed — that asymmetry is the root of the lingering-torque bug class.
- `integrate_forces.comp` reads external force/torque once per substep ([integrate_forces.comp:77](engine/Physics/shader/solver/XPBDSolver/integrate_forces.comp)); `XpbdConfig` is constructed inside `MainClass::LoadProject` ([MainClass.cpp:110-125](engine/Framework/MainClass.cpp)) and `XpbdGpuSolver` already has `SetConfig/GetConfig`.
- ComInertiaComputer's invertibility gate is an absolute `|det| > 1e-12` test at two call sites ([ComInertiaComputer.hpp](engine/Framework/Bridge/Internal/ComInertiaComputer.hpp)).

## Goals / Non-Goals

**Goals:**

- One uniform write-lifetime rule across all six `BodyField` channels, with no per-channel bookkeeping state in the app.
- The three policy-deployment capabilities (rate knobs, chain composition, small inertia) with regression tests that pin them.
- Engine changes limited to narrow, honest seams (solver-config parameter, force/torque consumption contract, invertibility gate).

**Non-Goals:**

- Runtime reconfiguration of solver timing (config is creation-time only).
- Per-slot upload granularity (whole-field-per-dirty-field stays).
- Changing `ExternalForce`/`ExternalTorque` vec4 layout or `BodyField` enumeration.
- Joint limit enforcement, actuator redesign, or multi-solver support beyond the clearing-aggregation rule.

## Decisions

### D1: Round-based write semantics — writes are consumed by the next Step

Every `SetBodyValue` / `AddJointTorque` write takes effect for one round (until the next `Step`). State channels carry their effect forward through the simulated state; force/torque are per-step quantities the solver consumes and clears.

Mechanism: extend the post-step readback to all six channels so the staging baseline is refreshed every step (for force/torque the refreshed value is the post-clear zero), and make the solver clear the external force/torque GPU buffers after the step's last substep. Both clears are required — they close different resurrection paths:

- Solver clear closes the "no write this round, GPU buffer still holds last round's value" path.
- Readback refresh closes the "this round writes slot 7, whole-field upload re-sends slot 5's stale staging value" path.

Forcing the force/torque staging to upload zeros every round was the alternative (host-only fix); rejected because it leaves the persistence contract as an app-side convention while the engine's GPU buffers stay implicitly persistent, inviting the same bug in every future writer (component init values, editor tools).

Clearing placement in the solver is after the substep loop, never inside it: `integrate_forces` runs once per substep and must see the force in every substep so the full `F·dt` impulse is applied. Clearing per substep would weaken forces by the substep count.

Paused-step consumption: a `Step` that does not integrate (`IsSimulationEnabled` false) does not clear the force/torque buffers — pending writes are deferred and consumed by the next integrating step. This falls out of placing the clear inside the integration branch, and matches the intuitive expectation that a force written while paused applies when the simulation resumes.

### D2: `AddJointTorque` accumulates; body writes assign; call order rules

`SetJointTorque` becomes `AddJointTorque` with `+=` semantics on the round's external-torque staging. This is the minimal fix for serial chains: a body shared by two joints receives both contributions because each call adds to the slot instead of overwriting it. Accumulation is the physical semantics of force composition; the API name states it.

`SetBodyValue(ExternalTorque)` keeps assignment semantics (last write wins per slot), so expert callers retain an explicit override. The two verbs coexist in one buffer with call-order resolution; the caller manages ordering. Documented consequence: a body-level assignment after a joint accumulate erases that joint's contribution for that body.

Rejected alternatives: a per-joint persistent `manual_τ` state layer with a per-step composition pass plus a sticky per-body override registry (preserves "set once" ergonomics but adds exactly the hidden-state machinery this design removes); `SetJointTorque` keeping assignment semantics on pairs (cannot express chains at all).

Not idempotent by design: re-issuing `AddJointTorque` in a round doubles the torque. Acceptable for a control-loop API where the loop body is the round; the test suite pins the "re-apply per step" pattern.

### D3: Solver-config seam at `LoadProject`, creation-time only

`MainClass::LoadProject` gains an optional `const XpbdConfig*` parameter; when present the default solver is registered with that config, otherwise the engine defaults apply. `PhysicsApp::Create` merges the `CreateInfo` timing knobs (0 = keep default) into a config and passes it down. This keeps the knobs out of `StartupOptions` (Core) and `MainClass` state, and reuses the existing creation path in `LoadProject`.

The config is consumed at solver registration; no `ISolver::SetConfig` is added to the interface. `XpbdGpuSolver::SetConfig` exists on the concrete class and remains usable there, but the abstract contract stays config-free so future solvers with different config types are not forced into `XpbdConfig`.

The `XpbdConfig` struct keeps its role as the XPBD solver's config type; the parameter is typed as `XpbdConfig` because `LoadProject` is the function that constructs an `XpbdGpuSolver`. The parameter is explicitly a temporary seam until project-level configuration exists (the project config file currently only carries `default_level`); when that lands, the config source moves there and the parameter is removed.

### D4: Invertibility gate — scale-relative, double precision, positive-definiteness check

Replace the absolute `|det| > 1e-12` test with `|det| > ε_machine · ‖I‖_F³` (determinant and Frobenius norm computed in double) plus a positive-definiteness check. Rationale:

- Absolute epsilons are invalid for inertia: magnitude scales with `mass·length²`, so small bodies (the A1 foot) are wrongly declared singular.
- `ε_machine · ‖I‖_F³` is derived from the type's precision — no hand-tuned constant — and is scale-invariant: conditioning, not magnitude, decides. The Frobenius norm is the standard companion of the `det vs norm³` form; `trace³` differs only by a constant factor and required a magic multiplier.
- Double precision avoids float underflow in the determinant of tiny tensors (the exact regime under test).
- `abs(det)` alone would admit negative-definite tensors; the manual-inertia path validates positive definiteness and throws `std::invalid_argument` on garbage input instead of silently inverting it (per decision in proposal).

The gate is one shared helper used by both the automatic and manual call sites so the two paths cannot diverge.

### D5: Supersede the ad-hoc worktree rather than layering on it

The uncommitted worktree changes are replaced wholesale: revert `OptionHandler.h` and `MainClass.{h,cpp}` physics fields, delete `actuated_slots`/`ReleaseActuatorSlots` and the `RemoveActuator` zeroing patch, keep (and re-home) the regression tests the worktree added — the chained-joint scenario and the tiny-inertia fixture are exactly the right pins for R2/R3.

## Risks / Trade-offs

- [Round semantics is a breaking behavior change for Python users relying on persistent force/torque] → README documents the re-apply-per-step pattern; test scenarios are rewritten to it; the RL loop shape (write every control tick) is unaffected in practice.
- [Solver-side clearing touches engine code used by all consumers] → the consumed-per-step contract is the physics standard (cf. Bullet `clearForces`, Isaac Gym apply-force semantics); `RigidBodyComponent::m_external_force/m_external_torque` become one-shot initial values — no in-tree consumer depends on persistence (verified by search).
- [Clearing after the substep loop is easy to get wrong (per-substep clearing silently weakens forces)] → spec scenario "Force survives all substeps of one step" pins the impulse; a unit-style scenario with 2+ substeps and `F·dt` verification catches regressions.
- [Readback extending to force/torque changes the staging buffer types (`StagingToDevice` → `ReadbackFromDevice|CopyFrom`)] → same allocation path as the four state channels (already proven), cost is two extra buffer copies per step — negligible at realistic body counts.
- [Call-order semantics can surprise users who interleave accumulate and assign on one slot] → documented prominently in the API docs; the alternative (hidden override registry) was rejected in D2 as more surprising.
- [Double-precision determinant in a hot path] → called once per body at scene build/flush only, not per step.

## Migration Plan

1. Implement engine seams first (D3 param, D4 gate, D1 solver clearing) — each independently landable.
2. Implement the app-side round mechanics (readback extension, staging type change) and the `AddJointTorque` rename in one commit — the spec deltas are only coherent together.
3. Rewrite affected test scenarios (`test_joint_torque.py`, `test_robot.py`, C++ mirror) to round semantics in the same commit; keep the chained-joint and tiny-inertia regressions green throughout (they may be adapted, not weakened).
4. Rollback: the change is self-contained in the listed files; reverting the merge restores persistent-write semantics. No data-format or serialization migration is involved.

## Open Questions

(none — the `Robot.set_joint_torques` alias question is resolved: no alias, clean break, matching the proposal's BREAKING markers.)
