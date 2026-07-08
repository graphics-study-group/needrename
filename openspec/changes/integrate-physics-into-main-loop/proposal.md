## Why

Physics simulation currently requires manual setup — components must be created at C++ level, a solver must be registered, and `InitializePendingRigidBodies` must be called manually. When a scene with physics components is loaded from a project, pressing Play in the editor produces no physics effects. The physics pipeline is absent from the editor loop entirely, and `MainClass::RunOneFrame` has the hooks but no solver is ever registered (they are no-ops).

## What Changes

- **BREAKING**: Physics component lifecycle is split: `Awake()` registers topology with `PhysicsScene` (one-time); `Init()` uploads current transform and property data (every Play). Previously all physics logic was in `Awake`.
- `MainClass` auto-registers a default `XpbdGpuSolver` with hardcoded config in `LoadProject()`, making physics "just work" when a project with physics objects is loaded.
- `PhysicsScene` gains new API methods: `SetRigidBodyTransform`, `SetModelMatrixActive`/`IsModelMatrixActive`, and joint allocate/update methods.
- `InitializePendingRigidBodies` is called at the loop level (after `ProcessEvents`, before `UpdateRendererData`) instead of requiring manual invocation.
- The editor main loop mirrors `MainClass::RunOneFrame`, gaining the full physics pipeline (PreGPUStep/GPUStep/PostGPUStep) gated on `m_is_playing`.
- `EditorRenderGraphBuilder` supports importing the physics model matrices SSBO, enabling physics-driven rendering in the editor.
- Editor `m_OnStop` callback disables physics simulation and clears SSBO model matrix activation.
- `PreRenderUpdate` checks `IsModelMatrixActive` before setting SSBO model matrix indices, ensuring `TransformComponent`-driven rendering when physics is not active.

## Capabilities

### New Capabilities

- `physics-main-loop-integration`: Default solver registration and automatic `InitializePendingRigidBodies` timing in the main loop, making physics work without manual setup.
- `physics-component-lifecycle`: Three-layer component lifecycle (Awake register / Init upload / Stop deactivate) for `RigidBodyComponent`, `CollisionShapeComponent`, and `PhysicsConstraintComponent`.
- `editor-physics-pipeline`: Editor loop physics pipeline (PreGPUStep/GPUStep/PostGPUStep) gated on play state, including SSBO import in the editor render graph.
- `physics-renderer-ssbo-toggle`: Model matrix source toggles between `TransformComponent` (push-constant) and physics SSBO based on simulation state.

### Modified Capabilities

<!-- No existing specs to modify -->

## Impact

- `engine/Physics/PhysicsScene.{h,cpp}`: New public API methods.
- `engine/Physics/PhysicsSystem.{h,cpp}`: `PreGPUStep` auto-calls `InitializePendingRigidBodies` if pending.
- `engine/Framework/component/physics/RigidBodyComponent.{h,cpp}`: Split `Awake`/`Init`, add `m_cached_shapes`.
- `engine/Framework/component/physics/CollisionShapeComponent.{h,cpp}`: Split `Awake`/`Init`.
- `engine/Framework/component/physics/PhysicsConstraintComponent.{h,cpp}`: Split `Awake`/`Init`, allocate/update pattern.
- `engine/Framework/component/RenderComponent/RendererComponent.cpp`: `PreRenderUpdate` checks `IsModelMatrixActive`.
- `engine/MainClass.{h,cpp}`: `SetupDefaultPhysics()` in `LoadProject()`, `InitializePendingRigidBodies` in `RunOneFrame`.
- `editor/Editor/Render/EditorRenderGraphBuilder.{h,cpp}`: Add `model_matrices_buffer` parameter.
- `example/editor_run_game_example/main.cpp`: Editor loop restructured with physics pipeline and start/stop callbacks.
- `docs/adr/0001-physics-component-lifecycle.md`, `docs/adr/0002-physics-main-loop-integration.md`: ADRs documenting decisions.
