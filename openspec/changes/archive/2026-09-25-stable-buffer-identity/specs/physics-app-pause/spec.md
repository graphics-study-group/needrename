## MODIFIED Requirements

### Requirement: CommitScene seeds initial model matrices

In rendering modes (`Windowed`, `Offscreen`), `CommitScene` SHALL produce model matrices once, before `SetSimulationEnabled(true)`, so that the initial model matrices are written from the `FlushPhysics`-seeded poses and the renderer never displays the buffer's initial contents.

The production SHALL be a direct model matrix call on the solver entry point, not a physics step: the step pipeline SHALL NOT be invoked, and no body SHALL advance during it.

Because a solver's model matrix output is only produced when a caller asks for it, and `FlushPhysics` does not seed the buffer, without this call the renderer would read the buffer's creation-time contents while paused.

#### Scenario: Bodies are visible while paused

- **WHEN** `CommitScene` returns in `Windowed` mode and `RenderNextFrame` runs while paused (before any `Step`)
- **THEN** bodies render at their initial pose (the frame shows more than the skybox)
- **AND** the bodies' state has not advanced (no physics integration ran)

#### Scenario: Seed pass does not advance simulation

- **WHEN** `CommitScene` returns and the first `Step` is then called
- **THEN** the simulation advances from the initial poses (the seed did not integrate)

#### Scenario: Seed does not run the step pipeline

- **WHEN** `CommitScene` produces the initial model matrices
- **THEN** no `PreGPUStep`, `GPUStep` or `PostGPUStep` call is made for that production
- **AND** only the model matrix entry point is invoked
