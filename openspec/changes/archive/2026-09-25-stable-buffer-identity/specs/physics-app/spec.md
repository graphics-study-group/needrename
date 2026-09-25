## MODIFIED Requirements

### Requirement: CommitScene performs one-way commit

`CommitScene` SHALL, in order: flush the command queue; add and process init events (component Init/Awake, joint setup); flush physics (adaptor conversion + GPU buffer sync); ensure the render-owned model matrices buffer can hold the scene's rigid body slot count and produce model matrices into it once; build the default render graph (via `ComplexRenderGraphBuilder`); and set the freeze flag. After commit the simulation SHALL start paused.

The model matrices buffer SHALL be obtained from the render system; `CommitScene` SHALL NOT forward a physics-owned buffer, and the render graph build SHALL take no model matrices argument.

#### Scenario: Commit sequence freezes and pauses

- **WHEN** `CommitScene` returns
- **THEN** all pending scene work is flushed, model matrices have been produced once, the render graph is built, the scene is frozen, and the simulation is paused

#### Scenario: Render graph is built without a model matrices argument

- **WHEN** `CommitScene` builds the default render graph
- **THEN** the builder imports the render system's model matrices buffer itself
- **AND** no buffer argument is passed to the builder

### Requirement: Step executes one pure physics step

`Step` SHALL advance the physics simulation by one fixed step (XpbdConfig timestep 1/60) using a dedicated command buffer, with device-level waitIdle before and after the physics submission. `Step` SHALL NOT process input events. Consecutive `Step` calls SHALL be allowed (physics fast-forward without rendering). While paused, `Step` SHALL remain callable and the solver layer SHALL no-op.

Because the step no longer produces model matrices as a side effect, `Step` SHALL also produce model matrices into the render system's buffer on the same command buffer after the step, so that a render frame following a step observes the poses that step produced.

#### Scenario: Step is repeatable
- **WHEN** `Step` is called twice in a row without `RenderNextFrame`
- **THEN** the physics simulation advances two steps without error

#### Scenario: Paused step does not evolve physics
- **WHEN** `Step` is called while paused
- **THEN** the physics state does not change

#### Scenario: A step refreshes model matrices
- **WHEN** `Step` advances the simulation and `RenderNextFrame` is called afterwards
- **THEN** the model matrices read by the render frame correspond to the poses the step produced
- **AND** the model matrices were recorded on the same command buffer as the step
