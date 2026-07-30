## Context

The engine has two main loops that diverge significantly:
- **Editor loop** (`example/editor_run_game_example/main.cpp`): Custom loop with ImGui, conditional input/tick, file drop import, composite render graph.
- **MainClass::RunOneFrame** (`engine/MainClass.cpp`): Standard loop with physics pipeline hooks, but no solver registration.

Physics components (`RigidBodyComponent`, `CollisionShapeComponent`, `PhysicsConstraintComponent`) currently do all registration and data upload in `Awake()`. In the editor, Stop/Start cycles re-fire only `Init()` — not `Awake()` — so physics state is never refreshed from current scene transforms.

The renderer determines model matrix source per-frame in `PreRenderUpdate()`: if a `RigidBody` is found in the ancestor chain, `model_mat_index = rigid_idx` (SSBO-driven); otherwise `model_mat_index = -1` (push-constant). There is no mechanism to toggle this based on simulation state.


## Goals / Non-Goals

**Goals:**
- Load a project with physics components → physics automatically functions on Play in both editor and standalone builds.
- Editor Stop/Start cycles refresh physics state from current scene transforms.
- Model matrix rendering switches cleanly between `TransformComponent` (stopped) and SSBO (playing).
- Editor loop structurally mirrors `MainClass::RunOneFrame` for physics pipeline.
- Physics pipeline gates on `m_is_playing` in the editor.
- `InitializePendingRigidBodies` is called automatically at the right point in the frame.

**Non-Goals:**
- Runtime addition/removal of physics objects while simulation is active.
- Scene state restoration on editor Stop (scene stays as-is).
- Editor Stop/Start modifying the GameObject/Component hierarchy.
- Project-level physics config file (config is hardcoded for now).
- Multi-scene physics support beyond the main scene.

## Decisions

### 1. Component Lifecycle: Awake Registers, Init Uploads

Physics components use a three-layer lifecycle:

| Lifecycle | When | RigidBodyComponent | CollisionShapeComponent | PhysicsConstraintComponent |
|-----------|------|--------------------|--------------------------|---------------------------|
| `Awake()` | Once (FlushCmdQueue) | `RegisterRigidBody` + `CollectShapesRecursively` + `SetCollisionShapeRigidBody` | `RegisterCollisionShape` + `TryAttachToAncestorRigidBody` | `AllocateFixedJoint` / `AllocateHingeJoint` (placeholder) |
| `Init()` | Every Play | `SetRigidBodyTransform` + `SetRigidBodyProperties` + `EnqueueRigidBodyInitialization` + `SetModelMatrixActive(true)` | `UpdateCollisionShapeGeometry` + `TryAttachToAncestorRigidBody` | `UpdateFixedJoint` / `UpdateHingeJoint` (resolve handles → indices) |
| Stop callback | Every Stop | (via `SetSimulationEnabled(false)`) | (via `SetSimulationEnabled(false)`) | (indices persist, no action) |

**Rationale**: `Awake` runs once and registers topology. `Init` runs every Play and uploads fresh data. This matches the editor lifecycle where `Awake` fires only at component creation, but `Init` fires on every Start. Constraint Init is guaranteed to find both RigidBodies because all RB Awakes precede all Inits (Awake fires during `FlushCmdQueue`, Init is queued and processed later during `ProcessEvents`).

**Alternative considered**: Move all physics logic to `Init` only. Rejected because (a) unordered `Init` ordering makes constraint resolution non-deterministic, (b) re-registration on every Play adds unnecessary work, (c) Awake-based registration with Init-based upload cleanly separates topology (persistent) from data (transient).

### 2. Solver Registration in MainClass::LoadProject

`MainClass` gains a private `SetupDefaultPhysics()` method called at the end of `LoadProject()`:

```cpp
void MainClass::SetupDefaultPhysics() {
    auto *physics_scene = world->GetMainSceneRef().GetPhysicsScene();
    if (!physics_scene || physics_scene->GetRigidBodyCount() == 0) return;

    auto solver = std::make_unique<XpbdGpuSolver>(*renderer);
    XpbdConfig config{};
    config.gravity = glm::vec3(0.0f, 0.0f, -9.81f);
    config.time_step = 1.0f / 60.0f;
    config.num_substep_perstep = 2;
    config.num_iter_persubstep = 50;
    config.num_velocity_iters = 10;
    config.max_contact_points = 50000u;
    config.contact_margin = 0.001f;
    config.grid_cell_size = 2.0f;
    config.grid_world_min = glm::vec3(-100.0f, -100.0f, -5.0f);
    config.grid_world_max = glm::vec3(100.0f, 100.0f, 20.0f);
    config.max_cells_per_shape = 8;
    config.max_global_shape_count = 128;
    config.fallback_all_pairs_threshold = 8;
    solver->SetConfig(config);
    physics->RegisterSolver(physics_scene->GetSceneID(), std::move(solver));
}
```

**Rationale**: Solver is conceptually part of project configuration. Placing it in `LoadProject` ensures it always runs after scene creation and component Awake. Config is hardcoded to match the physics example defaults. Will be superseded by a project-level config file later.

**Alternative considered**: Auto-create solver in `PhysicsSystem::CreateScene`. Rejected because (a) it doesn't have `RenderSystem` access, (b) config should be project-specific, not engine-global.

### 3. InitializePendingRigidBodies Timing

Called at the loop level after `ProcessEvents()` and before `UpdateRendererData()`:

```
ProcessEvents()                            ← Init fires, EnqueueRigidBodyInitialization
PhysicsSystem::InitializePendingRigidBodies(renderer)  ← Creates SSBO, uploads initial data
UpdateRendererData()                       ← PreRenderUpdate reads model_mat_index
```

**Rationale**: Ensures the SSBO model matrices buffer is GPU-ready before renderers query their `model_mat_index`. The call includes an early return when `m_rigid_body_init_queue` is empty, making idle frames a fast no-op (single `deque::empty()` check). Only frames where `Init` has enqueued new work trigger `RefreshGpuBuffers` and GPU submission. The `SubmitAndWait` inside `InitializePendingRigidBodies` is safe because it runs before `StartFrame()` (no in-flight GPU work).

### 4. Model Matrix SSBO Toggle

`PhysicsScene` gains `m_rigid_body_model_matrix_active` (vector<bool>) plus:
- `SetModelMatrixActive(idx, bool)` — called by `RigidBodyComponent::Init()` (true) and `SetSimulationEnabled(false)` (all false).
- `IsModelMatrixActive(idx)` — queried by `PreRenderUpdate()`.

`PreRenderUpdate` logic:
```cpp
if (found_rigid_body && physics_scene->IsModelMatrixActive(rigid_idx)) {
    model_mat_index = static_cast<int32_t>(rigid_idx);
    model = inverse(rb_world) * model;  // local offset
} else {
    model_mat_index = -1;  // push-constant only
    model = full_world_transform;
}
```

**Rationale**: Ties SSBO usage to simulation state. When stopped, renderers use `TransformComponent` directly. When playing, they compose SSBO rigid body transform with local offset. The transition is per-frame in `PreRenderUpdate`, requiring no render graph rebuild.

### 5. Editor Loop Alignment

Editor loop is restructured to match `MainClass::RunOneFrame`:

```cpp
// Main loop (simplified)
while (!onQuit) {
    TimeSystem::NextFrame();
    LoadAssetsInQueue();

    // Event polling
    SDL_PollEvent → gui + conditional game input + file drops

    // Input (conditional)
    if (m_accept_input) input->Update() else input->ResetAxes();

    FlushCmdQueue();

    if (m_is_playing) { AddTickEvent(); ProcessEvents(); }

    physics->InitializePendingRigidBodies(rsys);

    UpdateRendererData();

    gui->PrepareGUI();
    main_window.Render();

    rsys->StartFrame();

    if (m_is_playing) physics->PreGPUStep();

    auto cb = rsys->GetFrameManager().GetRawMainCommandBuffer();
    cb.begin(vk::CommandBufferBeginInfo{});
    if (m_is_playing) physics->GPUStep(cb);
    rg->RecordAllPasses(cb);
    cb.end();
    rsys->GetFrameManager().SubmitMainCommandBuffer();

    if (m_is_playing) physics->PostGPUStep();

    CompleteFrame();
}
```

Replaces `rg->Execute()` with manual CB management to share the command buffer between physics compute and render graph passes.

### 6. EditorRenderGraphBuilder SSBO Support

`BuildEditorRenderGraph` gains a `const ComputeBuffer *model_matrices_buffer = nullptr` parameter. When non-null, it imports the SSBO as an external resource (`ShaderRandomWrite`) and declares `UseBuffer(ShaderRandomRead)` on shadowmap, scene lit, and game lit passes.

The editor render graph is lazily rebuilt when the SSBO first becomes available (on first Play after `InitializePendingRigidBodies`). Initial build uses nullptr → no SSBO import. After SSBO creation, `BuildEditorRenderGraph` is called again with the real buffer.

**Rationale**: Avoids pre-initializing PhysicsScene just to satisfy render graph build-time SSBO requirement. The lazy rebuild happens exactly once per session.

### 7. Start/Stop Callbacks

```cpp
auto start = std::make_unique<FuncDelegate<void()>>([] {
    auto &scene = MainClass::GetInstance()->GetWorldSystem()->GetMainSceneRef();
    scene.ClearEventQueue();
    scene.AddInitEvent();
    if (auto *ps = scene.GetPhysicsScene()) {
        ps->SetSimulationEnabled(true);
    }
});

auto stop = std::make_unique<FuncDelegate<void()>>([] {
    auto &scene = MainClass::GetInstance()->GetWorldSystem()->GetMainSceneRef();
    if (auto *ps = scene.GetPhysicsScene()) {
        ps->SetSimulationEnabled(false);
    }
});
```

### 8. PhysicsScene API Additions

| Method | Signature |
|--------|-----------|
| `SetRigidBodyTransform` | `(uint32_t idx, const glm::vec3 &pos, const glm::quat &rot)` |
| `SetModelMatrixActive` | `(uint32_t idx, bool active)` |
| `IsModelMatrixActive` | `(uint32_t idx) const → bool` |
| `AllocateFixedJoint` | `() → uint32_t` |
| `UpdateFixedJoint` | `(uint32_t idx, uint32_t obj1, uint32_t obj2, float compliance, const glm::vec3 &rel_pos, const glm::quat &rel_rot)` |
| `AllocateHingeJoint` | `() → uint32_t` |
| `UpdateHingeJoint` | `(uint32_t idx, uint32_t obj1, uint32_t obj2, float compliance, const glm::vec3 &axis1, const glm::vec3 &axis2, const glm::vec3 &attach1, const glm::vec3 &attach2)` |

`SetSimulationEnabled(false)` is modified to also clear all `m_rigid_body_model_matrix_active` entries.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| `InitializePendingRigidBodies` calls `SubmitAndWait` — could stall if called mid-frame with in-flight GPU work | Called before `StartFrame()`, ensuring no in-flight work. Hypothetical future async loading would need re-evaluation. |
| Hardcoded `XpbdConfig` may be too heavy for some use cases | Documented as temporary; project config file will supersede. Config values match proven physics example defaults. |
| Constraint Init depends on all RB Awakes having completed before Init | Guaranteed by event queue design (Awake fires synchronously in `FlushCmdQueue`, Init is queued and processed later). |
| Editor render graph lazy rebuild requires re-calling `BuildEditorRenderGraph` | Single rebuild per session; render graph construction is O(passes) and not performance-critical during Play transition. |
| `EditorRenderGraphBuilder` SSBO import may change barrier behavior | The SSBO uses `ShaderRandomWrite` on import and `ShaderRandomRead` on use — same pattern as `ComplexRenderGraphBuilder`, proven in physics example. |

## Open Questions

- Should `XpbdConfig` default values differ between editor (lightweight) and standalone (heavy)? Deferred to project config file work.
- Should `SetSimulationEnabled(false)` on Stop also reset rigid body velocities to zero? Current decision: no, velocities persist. Can be revisited.
