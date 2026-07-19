# ADR-0001: Physics Component Lifecycle — Awake Registers, Init Uploads

## Status

Accepted

## Context

Physics components (`RigidBodyComponent`, `CollisionShapeComponent`, `PhysicsConstraintComponent`) need to register
with `PhysicsScene` and upload transform/property data. The engine has two lifecycle callbacks:

- **Awake()**: Fired once during `Scene::FlushCmdQueue()` when a component is first added.
- **Init()**:  Fired every time the scene enters "play" mode (editor Start, or initial load). Queued by `AddInitEvent()` and dispatched by `ProcessEvents()`.

In the editor, Stop/Start cycles re-fire `Init` but not `Awake`. Physics registration (topology) should persist across
cycles, while transform/property data (values) must be refreshed from the current scene state on each Play.

Additionally, the engine's model matrix rendering system switches between two paths:
1. **TransformComponent-driven** (`model_mat_index = -1`): Uses push-constant model matrix directly. Used when not playing.
2. **SSBO-driven** (`model_mat_index >= 0`): Composes `model_matrices[rigid_idx] * local_offset`. Used when physics simulation is active.

The SSBO path must not be active before physics is initialized.

## Decision

We split physics component lifecycle into three layers:

```
Awake:  Register topology with PhysicsScene, cache index (one-time)
        ├── RigidBodyComponent:     RegisterRigidBody → m_rigid_body_index
        ├── CollisionShapeComponent: RegisterCollisionShape → m_shape_index
        │                           TryAttachToAncestorRigidBody (best-effort)
        └── PhysicsConstraintComponent: AllocateFixedJoint/AllocateHingeJoint → indices

Init:   Upload current transform + properties to PhysicsScene using cached indices (every Play)
        ├── RigidBodyComponent:     SetRigidBodyTransform + SetRigidBodyProperties
        │                           + EnqueueRigidBodyInitialization + SetModelMatrixActive(true)
        ├── CollisionShapeComponent: UpdateCollisionShapeGeometry + TryAttachToAncestorRigidBody
        └── PhysicsConstraintComponent: UpdateFixedJoint/UpdateHingeJoint (resolve handles → indices)

Stop:   SetSimulationEnabled(false) → clears all model_matrix_active flags
```

The two-way binding pattern between RigidBody and CollisionShape handles unordered Awake:
- `RigidBodyComponent::Awake` collects shapes via `CollectShapesRecursively`, calls `SetCollisionShapeRigidBody`.
  Skips shapes not yet registered (`IsRegisteredInPhysicsScene` check).
- `CollisionShapeComponent::Awake` calls `TryAttachToAncestorRigidBody`.
  If RB not yet registered, skips.
- Whichever runs second completes the binding.

PhysicsConstraintComponent registers in Awake with placeholder joint slots. In Init, it resolves ObjectHandles to
rigid body indices (guaranteed available since all RB Awakes have completed before any Init runs).

## Known Limitations

The current design assumes no physics-related GameObjects or Components are added or removed while simulation is
running. The PhysicsScene internal arrays (rigid body SoA, shape SoA, joint vectors) do not support dynamic
insertion/deletion during gameplay, and index-based caching in components would be invalidated by such mutations.

These capabilities need to be designed and implemented in a future change:
- Hot-add / hot-remove of RigidBodyComponents during simulation
- Hot-add / hot-remove of CollisionShapeComponents during simulation
- Dynamic joint creation/destruction during simulation
- Index invalidation and reassignment in PhysicsScene SoA arrays

## Consequences

- Physics registration (topology) is one-time, survives editor Stop/Start cycles.
- Transform/property data is refreshed from current scene state on every Play via Init.
- Model matrix SSBO path is activated/deactivated cleanly via `model_matrix_active` flags, tied to simulation state.
- Requires new PhysicsScene APIs: `SetRigidBodyTransform`, `SetModelMatrixActive`/`IsModelMatrixActive`,
  `AllocateFixedJoint`/`UpdateFixedJoint`, `AllocateHingeJoint`/`UpdateHingeJoint`.
- Constraint Init is guaranteed to find both RigidBodies because all RB Awakes precede all Inits.
