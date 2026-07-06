## 1. ISolver interface refactoring

- [x] 1.1 Update `ISolver` method signatures in `engine/Physics/Solver/ISolver.h`: remove `RenderSystem &` and `PhysicsScene &` from `PreGPUStep`, `GPUStep`, `PostGPUStep`. GPUStep now takes only `vk::CommandBuffer cb`.
- [x] 1.2 Add `virtual void OnBindToScene(PhysicsScene &scene)` to `ISolver` with default implementation that sets `m_bound_scene = &scene`
- [x] 1.3 Add `protected: PhysicsScene *m_bound_scene = nullptr` member to `ISolver`
- [x] 1.4 Update doc comments on all ISolver methods to reflect new signatures and binding semantics

## 2. PhysicsSystem refactoring

- [x] 2.1 Change `m_solvers` from `std::vector<std::unique_ptr<ISolver>>` to `std::unordered_map<uint32_t, std::vector<std::unique_ptr<ISolver>>>` in `PhysicsSystem.h`
- [x] 2.2 Change `RegisterSolver` signature to `RegisterSolver(uint32_t scene_id, std::unique_ptr<ISolver> solver)`, validate scene existence, call `OnBindToScene`
- [x] 2.3 Rewrite `PreGPUStep()`, `GPUStep(vk::CommandBuffer cb)`, `PostGPUStep()` to iterate all scenes and their bound solvers; remove `RenderSystem &` parameter
- [x] 2.4 Remove hardcoded `GetScenePtr(1)` calls from all three step methods
- [x] 2.5 Update doc comments on PhysicsSystem methods

## 3. DummySolver update

- [x] 3.1 Update `DummySolver` overrides in `DummySolver.h` to match new `ISolver` signatures
- [x] 3.2 Update `DummySolver::PreGPUStep` implementation in `DummySolver.cpp`: use `m_bound_scene` and stored `RenderSystem &`
- [x] 3.3 Update `DummySolver::GPUStep` implementation: use `m_bound_scene` and stored `RenderSystem &`
- [x] 3.4 Update `DummySolver::BuildRenderGraph`: replace `scene` parameter with access via `m_bound_scene`

## 4. Caller site updates

- [x] 4.1 Update `engine/MainClass.cpp`: change `physics->PreGPUStep(*this->renderer)` to `physics->PreGPUStep()`; same for `GPUStep` (remove first argument) and `PostGPUStep`
- [x] 4.2 Update `example/physics_example/main.cpp`: change `RegisterSolver(std::move(dummy_solver))` to `RegisterSolver(scene_id, std::move(dummy_solver))`

## 5. Verification

- [x] 5.1 Build the engine (`cmake --build build`) and verify no compile errors
- [x] 5.2 Run the physics example to verify solvers still work correctly
- [x] 5.3 Run `clang-format` on all modified files
