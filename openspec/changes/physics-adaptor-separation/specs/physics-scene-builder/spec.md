## MODIFIED Requirements

### Requirement: SceneBuilder finalizes physics initialization
`SceneBuilder::Finalize()` SHALL call `Scene::FlushPhysics(render_system)`. The method no longer takes a `PhysicsScene&` parameter directly — it operates through the Scene owning the created GameObjects.

#### Scenario: Physics initialization
- **WHEN** `Finalize()` is called after creating all physics objects
- **THEN** `scene.FlushPhysics(render_system)` is invoked, processing all pending descriptors through the Adaptor and syncing GPU buffers
- **AND** all rigid bodies are ready for simulation
