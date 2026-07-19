# ADR-0002: Physics Main Loop Integration

## Status

Accepted

## Context

The engine has two main loop variants:

1. **Editor loop** (`example/editor_run_game_example/main.cpp`): Custom loop with ImGui, conditional input (game widget focus), conditional tick (`m_is_playing`), file drop import, composite render graph (Scene + Game + GUI views).
2. **MainClass::RunOneFrame** (`engine/MainClass.cpp`): Standard loop with physics pipeline hooks (PreGPUStep → GPUStep → PostGPUStep), but no solver is ever registered (hooks are no-ops).

For physics to work automatically when a scene with physics components is loaded:
- A default XPBD GPU solver must be registered.
- `PhysicsScene::InitializePendingRigidBodies` must be called to create GPU buffers (including the `model_matrices` SSBO).
- Both loops must share the same command buffer for physics compute + rendering.

## Decision

### Solver Registration

MainClass registers a default `XpbdGpuSolver` in `LoadProject()` (after scene is loaded, before game loop):

```cpp
void MainClass::LoadProject(const std::filesystem::path &path) {
    // ... existing project loading ...
    auto *physics_scene = world->GetMainSceneRef().GetPhysicsScene();
    if (physics_scene && physics_scene->GetRigidBodyCount() > 0) {
        auto solver = std::make_unique<XpbdGpuSolver>(*renderer);
        solver->SetConfig(XpbdConfig{/* hardcoded defaults */});
        physics->RegisterSolver(physics_scene->GetSceneID(), std::move(solver));
    }
}
```

Config is hardcoded for now. A project-level physics config file will supersede this later.

### InitializePendingRigidBodies Timing

Called at the loop level, after `ProcessEvents()` and before `UpdateRendererData()`:

```
ProcessEvents()                           ← Init fires, enqueues rigid body init
physics->InitializePendingRigidBodies()   ← Creates GPU buffers, uploads initial data
UpdateRendererData()                      ← PreRenderUpdate reads SSBO indices
```

This ensures SSBO model matrices are GPU-ready before renderers query them.

### Editor Loop Alignment

The editor loop mirrors `MainClass::RunOneFrame` with editor-specific additions:

```
TimeSystem::NextFrame()
LoadAssetsInQueue()
SDL_PollEvent: GUI + conditional game input + file drops
input->Update() or ResetAxes()
FlushCmdQueue()
if (m_is_playing): AddTickEvent + ProcessEvents()
physics->InitializePendingRigidBodies()
UpdateRendererData()
gui->PrepareGUI()
main_window.Render()
rsys->StartFrame()
if (m_is_playing): physics->PreGPUStep()
cb.begin
  if (m_is_playing): physics->GPUStep(cb)
  rg->RecordAllPasses(cb)
cb.end + Submit
if (m_is_playing): physics->PostGPUStep()
CompleteFrame()
```

Physics pipeline is gated on `m_is_playing`. When stopped, no physics steps execute.
The command buffer is shared: physics compute + render graph passes on the same CB (same pattern as `MainClass::RunOneFrame`).

### Render Graph and SSBO

`EditorRenderGraphBuilder` is extended with a `model_matrices_buffer` parameter. The render graph is lazily rebuilt
when the SSBO becomes available (on first Play after `InitializePendingRigidBodies` creates it).

### Play/Stop Callbacks

```
m_OnStart: ClearEventQueue + AddInitEvent + SetSimulationEnabled(true)
m_OnStop:  SetSimulationEnabled(false)  // auto-clears model_matrix_active flags
```

## Known Limitations

Physics component creation/deletion during runtime (while simulation is running) is not supported. Adding or removing
`RigidBodyComponent`, `CollisionShapeComponent`, or `PhysicsConstraintComponent` after the initial scene load, and
especially while the solver is actively stepping, may result in index mismatches, dangling references, or undefined
behavior in the GPU pipeline. See ADR-0001 for details on what needs future design work.

## Consequences

- Loading a project with physics components → physics automatically works on Play.
- Editor play/stop cleanly activates/deactivates physics simulation and SSBO model matrix path.
- Physics compute and rendering share the same command buffer, ensuring proper barrier synchronization.
- Solver configuration is hardcoded; will be moved to project config file when that system is ready.
