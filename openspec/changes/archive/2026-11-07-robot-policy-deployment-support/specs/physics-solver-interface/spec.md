# physics-solver-interface Delta

## ADDED Requirements

### Requirement: External force and torque are per-step quantities

The rigid-body `external_force` and `external_torque` GPU buffers SHALL be
per-step quantities: a step's integration applies the values present in the
buffers and consumes them, clearing the buffers after the step's last substep
so the values cannot act again on a later step. Clearing SHALL happen after
all substeps of the step have integrated (never inside the substep loop), so a
force applied across N substeps contributes its full impulse `F * dt` over the
step.

A step that does not integrate (simulation disabled / paused) SHALL leave the
buffers untouched: pending values survive and are consumed by the next step
that actually integrates.

When multiple solvers are registered for one scene, the clearing SHALL be
aggregated at `PhysicsSystem::GPUStep` after all solvers have run, so no
solver's consumption erases values another solver has not yet seen.

Every solver implementation SHALL honor this contract, including the CPU
`DummySolver`.

#### Scenario: Force survives all substeps of one step

- **WHEN** a non-zero external force is uploaded and a step with N substeps
  (N > 1) runs
- **THEN** the force contributes its full `F * dt` impulse across the step's
  substeps (it is not consumed by the first substep)

#### Scenario: Buffer is cleared after the step

- **WHEN** a non-zero external force is uploaded and one step runs with no
  further writes
- **THEN** the next step's integration applies no residual force

#### Scenario: Paused step does not consume

- **WHEN** a non-zero external force is uploaded and a step runs while
  simulation is disabled
- **THEN** the force values remain in the buffers and are consumed by the next
  step that integrates

#### Scenario: Clearing is aggregated across solvers

- **WHEN** two solvers are registered for one scene and a step runs
- **THEN** both solvers observe the step's external force/torque values, and
  the buffers are cleared once after both have run
