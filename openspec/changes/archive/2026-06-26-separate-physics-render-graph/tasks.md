## 1. RenderGraph — Modify RecordAllPasses

- [x] 1.1 Modify `RenderGraph::RecordAllPasses` in `RenderGraph.cpp` — remove internal `cb.begin()` / `cb.end()`; method becomes pure recorder
- [x] 1.2 Modify `RenderGraph::Execute` in `RenderGraph.cpp` — add `cb.begin()` / `cb.end()` around `RecordAllPasses`, preserving original behavior
- [x] 1.3 Add `GetNumPasses() const noexcept` to `RenderGraph.h` / `RenderGraph.cpp`
- [x] 1.4 Update `test/compute_buffer_test.cpp` — add `cb[0].begin()` / `cb[0].end()` around `rg->RecordAllPasses(cb[0])`

## 2. ISolver Base Class

- [x] 2.1 Create `engine/Physics/Solver/ISolver.h`:
  - `virtual ~ISolver() = default`
  - `virtual void PreGPUStep(RenderSystem &, PhysicsScene &) {}` — no-op default
  - `virtual void GPUStep(RenderSystem &, PhysicsScene &, vk::CommandBuffer cb) = 0` — pure virtual, receives CB for RG recording
  - `virtual void PostGPUStep(RenderSystem &, PhysicsScene &) {}` — no-op default
  - `virtual bool IsInitialized() const noexcept = 0`
  - Forward-declare `vk::CommandBuffer` via `namespace vk { struct CommandBuffer; }`
  - NO `GetRenderGraph()` — RG is private to solver

## 3. DummySolver Compute Shader

- [x] 3.1 Create directory `engine/Physics/shader/solver/DummySolver/`
- [x] 3.2 Write `dummy_solver.comp` — Z displacement + model matrix in one dispatch

## 4. DummySolver Implementation

- [x] 4.1 Create `engine/Physics/Solver/DummySolver.h` — PImpl, inherits `ISolver`
- [x] 4.2 Implement `DummySolver.cpp`:
  - `GPUStep(system, scene, cb)`: ensure shaders loaded, update uniform buffer, lazily build RG, call `m_rg->RecordAllPasses(cb)`
  - `BuildRenderGraph()` (private): import scene buffers once, add compute pass, `BuildRenderGraph()`, call `SceneDataManager::SetModelMatricesBuffer()`
  - NO `GetRenderGraph()` override needed
- [x] 4.3 Uniform buffer updated each frame in `GPUStep()`: `vec4(gravity.xyz, time_step)`

## 5. PhysicsSystem — Three-Phase Step

- [x] 5.1 Add `#include <vector>`, forward-declare `ISolver`, `RenderSystem`, `vk::CommandBuffer` in `PhysicsSystem.h`
- [x] 5.2 Add `RegisterSolver(std::unique_ptr<ISolver>)` declaration
- [x] 5.3 Add `PreGPUStep(RenderSystem &)` declaration — iterates solvers calling `PreGPUStep`
- [x] 5.4 Add `GPUStep(RenderSystem &, vk::CommandBuffer cb)` declaration — iterates solvers calling `GPUStep(cb)`
- [x] 5.5 Add `PostGPUStep(RenderSystem &)` declaration — iterates solvers calling `PostGPUStep`
- [x] 5.6 Add `std::vector<std::unique_ptr<ISolver>> m_solvers` private member
- [x] 5.7 Implement `RegisterSolver`, `PreGPUStep`, `GPUStep`, `PostGPUStep` in `PhysicsSystem.cpp`
- [x] 5.8 REMOVE old single `GPUStep(RenderSystem&)` and `GetSolvers()` methods — replaced by three-phase API

## 6. ComplexRenderGraphBuilder — Model Matrices Input

- [x] 6.1 Add `BuildDefaultRenderGraph(w, h, id, const ComputeBuffer *model_matrices_buffer = nullptr)` overload
- [x] 6.2 When non-null: import with `prev_access = ShaderRandomWrite`, add `UseBuffer(ShaderRandomRead)` to shadow + lit passes

## 7. MainClass — Three-Phase Physics Call

- [x] 7.1 Include `<Physics/Solver/ISolver.h>` in `MainClass.cpp`
- [x] 7.2 In `RunOneFrame()`, after `StartFrame()`:
  - `this->physics->PreGPUStep(*this->renderer)` — before cb.begin
  - `cb.begin(...)` → `this->physics->GPUStep(*this->renderer, cb)` → rendering `RecordAllPasses` → `cb.end()` → `SubmitMainCommandBuffer()`
  - `this->physics->PostGPUStep(*this->renderer)` — after submit

## 8. Physics Example Refactoring

- [x] 8.1 Replace `PhysicsExampleRenderGraphBuilder` include with `DummySolver.h` + `ComplexRenderGraphBuilder.h`
- [x] 8.2 Create `DummySolver`, set config, register with `PhysicsSystem`
- [x] 8.3 Create `ComplexRenderGraphBuilder`, build RG with model matrices buffer
- [x] 8.4 Delete `PhysicsExampleRenderGraphBuilder.h/.cpp`
- [x] 8.5 Remove from `example/physics_example/CMakeLists.txt`

## 9. Build System

- [x] 9.1 Verify CMake auto-picks up new `.comp` (via GLOB_RECURSE) ✓
- [x] 9.2 Verify CMake auto-picks up new `.cpp`/`.h` (via GLOB_RECURSE) ✓
- [x] 9.3 Remove PhysicsExampleRenderGraphBuilder from example CMakeLists.txt ✓

## 10. Verification

- [x] 10.1 `EngineLibPhysics` compiles ✓
- [x] 10.2 `engine` (shared library) compiles and links ✓
- [x] 10.3 `physics_example` compiles and links ✓
- [x] 10.4 `compute_buffer_test` compiles (RecordAllPasses change) ✓
- [x] 10.5 `editor_run_game_example` compiles (backward compat) ✓
- [x] 10.6 Runtime: run physics_example, enable simulation, verify bodies move -Z
- [x] 10.7 Runtime: verify no Vulkan validation errors
