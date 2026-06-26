## Context

Currently `PhysicsSystem` manages two independent collections — `m_scene_map` (keyed by `scene_id`) and `m_solvers` (a flat vector with no scene association). When dispatching the three-phase step, `PhysicsSystem` hardcodes `GetScenePtr(1)` to select the target scene, then iterates all solvers passing the scene as a parameter. Solvers receive `RenderSystem&` in every method despite already storing it at construction time.

The proposed changes affect three layers:
- **ISolver** — the abstract interface all physics solvers implement
- **PhysicsSystem** — the engine-level manager that owns scenes and solver-to-scene bindings
- **DummySolver** — the current concrete ISolver implementation (and template for future solvers)

XPBDGpuSolver is explicitly out of scope — it does not currently implement ISolver and will be migrated separately.

## Goals / Non-Goals

**Goals:**
- Remove `RenderSystem&` and `PhysicsScene&` from `ISolver` method parameters — solvers access these via stored references
- Bind each solver to a specific `PhysicsScene` at registration time via `OnBindToScene`
- Eliminate the hardcoded `scene_id=1` in `PhysicsSystem` step dispatch
- Support per-scene multi-solver registration (`map<scene_id, vector<ISolver>>`)
- Minimal changes to caller sites (`MainClass::RunOneFrame`, `physics_example/main.cpp`)

**Non-Goals:**
- Migrate XPBDGpuSolver to ISolver (separate future change)
- Add scene creation/destruction lifecycle management beyond solver binding
- Support solver unbinding or rebinding to a different scene
- Change the RenderGraph ownership model (solver still owns its RG)
- Add parallel scene stepping (all scenes are stepped sequentially)

## Decisions

### Decision 1: Use `OnBindToScene` virtual function instead of constructor binding

**Chosen:** Virtual `OnBindToScene(PhysicsScene&)` called by `PhysicsSystem::RegisterSolver`.

**Rationale:** Using the constructor to bind `PhysicsScene&` would impose a construction order constraint — the scene must exist before the solver can be constructed. This is unnecessarily restrictive. The physics example creates the scene (via `LoadProject` → `CreateScene`), builds physics objects, then later creates and registers solvers. With `OnBindToScene`, the construction order remains flexible. Additionally, the virtual function is optional to override — solvers that don't need scene access don't have to implement it.

**Alternatives considered:**
- Constructor parameter: `Solver(RenderSystem&, PhysicsScene&)` — clean but imposes scene-first creation order; rejected because solvers may be created before their target scene exists (e.g., during engine initialization before project load)
- Setter method: `SetScene(PhysicsScene*)` — simple but easy to forget to call; `OnBindToScene` ties the binding to the registration act which is harder to misuse

### Decision 2: Protected `m_bound_scene` member in ISolver base class

**Chosen:** `PhysicsScene *m_bound_scene = nullptr` as a protected member, set by the default `OnBindToScene` implementation.

**Rationale:** The default `OnBindToScene` sets `m_bound_scene = &scene`, so most subclasses don't need to override it. Subclasses that need custom binding logic (e.g., additional setup, validation) can override. Making it protected (not private) allows subclasses direct access without going through a getter, which would add unnecessary indirection for the hottest path (every frame).

### Decision 3: `PhysicsSystem::RegisterSolver` takes `scene_id`

**Chosen:** `void RegisterSolver(uint32_t scene_id, std::unique_ptr<ISolver> solver)`

**Rationale:** PhysicsSystem owns the scene registry (`m_scene_map`), so it can validate that the scene exists. If the scene doesn't exist yet, the registration fails (log warning + return; no exception for non-fatal misuse). The solver is moved into a `map<uint32_t, vector<unique_ptr<ISolver>>>` keyed by `scene_id`.

**Alternatives considered:**
- Register on `PhysicsScene` directly: `scene.RegisterSolver(std::move(solver))` — would require PhysicsScene to know about ISolver, creating a circular dependency (PhysicsScene currently doesn't include ISolver.h). Also blurs responsibility between data storage (scene) and orchestration (system).
- `RegisterSolver(PhysicsScene&, solver)`: Caller must look up the scene first, but it's slightly more verbose at the call site with no real benefit.

### Decision 4: Step methods iterate all scenes

**Chosen:** For each scene → for each solver. Scene with no solvers is a no-op (no dispatch to empty solver list).

```cpp
void PhysicsSystem::PreGPUStep() {
    for (auto &[scene_id, scene] : m_scene_map) {
        auto it = m_solvers_per_scene.find(scene_id);
        if (it == m_solvers_per_scene.end()) continue;
        for (auto &solver : it->second) {
            solver->PreGPUStep();
        }
    }
}
```

**Rationale:** Simple, deterministic iteration. Scenes are stepped in map order (sorted by `scene_id`), solvers per scene in registration order. This preserves the existing "solvers iterated in registration order" guarantee from the spec but extends it per-scene.

### Decision 5: Remove scene_id parameter from PhysicsSystem step methods

**Chosen:** `PreGPUStep()`, `GPUStep(vk::CommandBuffer)`, `PostGPUStep()` — no parameters beyond the CommandBuffer for GPUStep.

**Rationale:** `RenderSystem&` was the only other parameter, and it was already stored by each solver at construction. Removing it simplifies the call sites in `MainClass::RunOneFrame`. The caller no longer needs to pass `*this->renderer`.

If per-scene stepping is needed in the future, overloads like `PreGPUStep(uint32_t scene_id)` can be added without breaking the no-argument version.

### Decision 6: New ISolver method signatures

```cpp
class ISolver {
public:
    virtual ~ISolver() = default;

    virtual void OnBindToScene(PhysicsScene &scene) { m_bound_scene = &scene; }
    virtual void PreGPUStep() {}
    virtual void GPUStep(vk::CommandBuffer cb) = 0;
    virtual void PostGPUStep() {}
    [[nodiscard]] virtual bool IsInitialized() const noexcept = 0;

protected:
    PhysicsScene *m_bound_scene = nullptr;
};
```

Each method's parameter removal rationale:
- `PreGPUStep`: `RenderSystem&` was unused by DummySolver (uses `m_impl->render_system`); `PhysicsScene&` is now `m_bound_scene`
- `GPUStep`: Both parameters redundant for the same reasons
- `PostGPUStep`: Same

## Risks / Trade-offs

- **m_bound_scene null safety**: If a solver is somehow invoked before registration (should not happen with current code), `m_bound_scene` is null. → Each solver checks `m_bound_scene` before accessing it; the default implementations are no-ops so the risk is confined to `GPUStep` overrides.
- **Scene destruction ordering**: If a scene is destroyed while its solvers still hold `m_bound_scene` raw pointers (via `DestroyScene`), the pointer dangles. → `DestroyScene` should be called only when no solvers reference the scene. This is an existing issue (solvers currently receive a dangling reference through the method parameter too). A future change can add unregister/cleanup, but this is not in scope.
- **map iteration determinism**: `std::unordered_map` iteration order is non-deterministic for scene dispatch. → In practice, the number of physics scenes will be very small (1–3). If strict ordering matters later, switch to `std::map`.

## Open Questions

None — all design decisions resolved through exploration discussion with the user.
