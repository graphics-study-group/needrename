# physics-main-loop-integration

## MODIFIED Requirements

### Requirement: MainClass forwards model matrices buffer to SceneDataManager

`MainClass::RunOneFrame` SHALL forward the physics model matrices buffer to the render system's `SceneDataManager` via `SetModelMatricesBuffer`, using the main scene's physics scene `GetGpuBuffers().model_matrices`. The forward SHALL happen after the physics flush/step and before render-graph recording in the same frame, and SHALL tolerate a null physics scene (skip the forward).

The forwarded value SHALL keep the buffer alive at the render side: the physics scene replaces this buffer whenever its slot count changes, so a borrowed raw pointer would dangle as soon as that happens. Forwarding a stably owned reference is what makes the render side independent of when the physics side resizes.

#### Scenario: Forward after physics step

- **WHEN** `RunOneFrame` executes with a registered physics scene containing GPU buffers
- **THEN** `SceneDataManager::SetModelMatricesBuffer` is called with the physics model matrices buffer before render passes are recorded

#### Scenario: No physics scene skips forward

- **WHEN** `RunOneFrame` executes and the main scene has no physics scene (or physics disabled)
- **THEN** no call to `SetModelMatricesBuffer` is made and rendering proceeds with no model matrices buffer

#### Scenario: Physics replaces the buffer after the forward

- **WHEN** the physics scene's slot count changes and it replaces its model matrices buffer between two forwards
- **THEN** the previously forwarded buffer remains valid for the render side until it is replaced by the next forward
- **AND** no render pass observes a destroyed buffer
