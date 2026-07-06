## MODIFIED Requirements

### Requirement: GPUStep owns multi-RG recording

`XpbdGpuSolver::GPUStep(vk::CommandBuffer cb)` SHALL record physics compute passes to `cb` using independently-managed RenderGraphs. The method SHALL:

1. Read GPU buffers from `m_bound_scene`; early-return if no alive bodies
2. Build any RGs that are null or have stale cached parameters (body_count, max_contacts, joint_counts, shape_count)
3. Notify `SceneDataManager::SetModelMatricesBuffer(gpu.model_matrices)`
4. Record RGs in sequence:
   ```
   for each substep:
       PreCollisionRG.RecordAllPasses(cb)
       broad_detector->Detect(cb)
       narrow_detector->Detect(cb)
       PostCollisionPreIterRG.RecordAllPasses(cb)
       for each position iteration:
           PositionIterRG.RecordAllPasses(cb)
       PostPositionRG.RecordAllPasses(cb)
       for each velocity iteration:
           VelocityIterRG.RecordAllPasses(cb)
   ModelMatrixRG.RecordAllPasses(cb)
   ```

The `PreCollisionRG` internal pass order SHALL be: substep-start position/orientation snapshots → integrate forces → pre-contact velocity snapshots → update shape world poses. This ensures pre-contact velocity snapshots capture post-integration velocity (including gravity) for correct restitution reference in the velocity solver.

The contact constraint passes within PositionIterRG and VelocityIterRG SHALL import `ShapeLocalPosition` and `ShapeLocalRotation` buffers (read-only) instead of `SubstepStartPosition` and `SubstepStartOrientation`. `SubstepStartPosition` and `SubstepStartOrientation` SHALL only be imported by PreCollisionRG (snapshot write, before force integration) and PostPositionRG (read for velocity update).

#### Scenario: Contact solver passes import shape local buffers

- **WHEN** PositionIterRG is built
- **THEN** the contact accumulation pass imports `ShapeLocalPosition` and `ShapeLocalRotation` as read-only external resources
- **AND** does NOT import `SubstepStartPosition` or `SubstepStartOrientation`

#### Scenario: Substep-start buffers are only used by velocity update

- **WHEN** GPUStep records all RGs in sequence
- **THEN** `SubstepStartPosition` and `SubstepStartOrientation` are written by PreCollisionRG (snapshot pass, before force integration)
- **AND** read by PostPositionRG (velocity update pass)
- **AND** NOT imported by PositionIterRG or VelocityIterRG

#### Scenario: Velocity iteration RG imports shape local buffers

- **WHEN** VelocityIterRG is built
- **THEN** the velocity accumulation pass imports `ShapeLocalPosition` and `ShapeLocalRotation` as read-only external resources
- **AND** does NOT import `SubstepStartPosition` or `SubstepStartOrientation`

#### Scenario: Pre-contact velocity snapshots capture post-integration state

- **WHEN** PreCollisionRG records its passes
- **THEN** the `Integrate Forces` pass executes before the `PreContactLinearVelocity` and `PreContactAngularVelocity` snapshot passes
- **AND** the snapshot passes read `RigidBodyLinearVelocity` and `RigidBodyAngularVelocity` after gravity and external forces have been applied
- **AND** no `UseBuffer` or `prev_access` changes are needed — the RG builder handles internal barriers between the reordered passes
