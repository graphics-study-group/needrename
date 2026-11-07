# physics-app-body-write Delta

## REMOVED Requirements

### Requirement: Direct overwrite with caller-managed lifetime

**Reason**: The persistent-until-overwritten write lifetime is replaced by
round-based semantics (writes are consumed by the next `Step`); the old
scenarios encode the removed persistence contract, so the requirement is
replaced wholesale rather than modified.

**Migration**: Re-apply `ExternalForce`/`ExternalTorque` writes before every
`Step` to keep them acting; state writes need no change (their effect persists
through the simulated state). See the ADDED requirement "Writes take effect for
one round".

## ADDED Requirements

### Requirement: Writes take effect for one round

Every `SetBodyValue` write SHALL take effect for one round — the interval until
the next `Step` consumes it. A step's upload carries the round's complete
per-field picture (written slots plus the current baseline for every other
slot), the solve applies it, and afterwards the written values are gone.

State fields (`Position`, `Rotation`, `LinearVelocity`, `AngularVelocity`)
carry their effect forward through the simulated state itself: a position set
once starts the body from that pose and integration evolves it naturally from
there, and the post-step readback replaces the staging baseline so stale state
writes never resurface.

`ExternalForce` and `ExternalTorque` SHALL be per-step quantities: the step's
integration applies them and the solver clears them afterwards (after the last
substep of the step). A force or torque set once therefore acts for exactly one
step, contributing an impulse of `value * dt` over that step regardless of the
substep count. Keeping a force acting SHALL require re-applying it every round.

Within one round, writes SHALL resolve by call order per slot: a later
`SetBodyValue` assignment overwrites whatever landed on that slot earlier in
the round (including accumulated joint-torque contributions), and a value from
an earlier round SHALL NOT resurface in a later one.

A `Step` that does not integrate (paused / simulation disabled) SHALL NOT
consume the round's `ExternalForce`/`ExternalTorque`: those values stay pending
and apply on the next step that actually integrates.

#### Scenario: Force applies for exactly one step

- **WHEN** `SetBodyValue(id, ExternalForce, F)` is called and `Step` runs twice
  with no further writes
- **THEN** the force acts on the first step's integration only (impulse
  `F * dt`) and the second step applies no residual force

#### Scenario: Sustained force requires re-application

- **WHEN** `SetBodyValue(id, ExternalForce, F)` is called before every `Step` in
  a loop
- **THEN** the force keeps acting on each step of the loop

#### Scenario: No resurrection across rounds

- **WHEN** a force is written to one body in one round and a different body's
  force is written in the next round
- **THEN** the first body's force does not reappear in the second round's step

#### Scenario: Position set once does not snap back

- **WHEN** `SetBodyValue(id, Position, P)` is called once and `Step` runs twice
  without further writes
- **THEN** after the first step the body's position starts from P
- **AND** after the second step the body's position has evolved from the
  integrated state (it is not reset to P)

#### Scenario: Later write wins within the round

- **WHEN** `AddJointTorque(joint, tau)` is followed by
  `SetBodyValue(child, ExternalTorque, zero)` before the next step
- **THEN** the child body's external torque is zero for that step while the
  parent body keeps its component of the joint torque

#### Scenario: Pending force survives pause

- **WHEN** `SetBodyValue(id, ExternalForce, F)` is called while `Step` does not
  integrate (paused), and the simulation later resumes and steps
- **THEN** the force applies on the first step that integrates and is consumed
  there
