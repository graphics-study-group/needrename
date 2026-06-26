## Why

The current GPU physics system (XPBD solver + collision detectors) injects its compute passes into the **same RenderGraphBuilder** as the rendering passes. This forces all passes — physics and rendering — to be constructed in a single `BuildRenderGraph()` call before the main loop. The resulting architecture has poor separation of concerns: solver "step" functions actually execute only once during graph construction, and per-frame dispatch happens inside opaque `AddPass` lambdas. Physics simulation cannot be driven by a natural `PhysicsSystem::Step()` call; it is instead a side effect of the rendering render graph's execution.

We need physics to own its own RenderGraph so that `PhysicsSystem::Step()` can be called each frame with a proper imperative flow, while still sharing the same Vulkan command buffer and queue with rendering for barrier-correct synchronization.

## What Changes

- **New Solver abstraction**: Introduce `ISolver` base class with a `Step(RenderSystem&, PhysicsScene&)` interface, enabling `PhysicsSystem` to manage multiple solvers uniformly
- **DummySolver prototype**: A minimal solver that displaces all rigid bodies along `-Z` by a configurable amount and writes model matrices — proving the architecture end-to-end without XPBD complexity
- **PhysicsSystem gets per-frame Step**: `PhysicsSystem::Step(render_system)` iterates registered solvers and calls `ISolver::Step()` for each
- **RenderGraph supports multi-graph command buffer sharing**: New `RecordToCommandBuffer(vk::CommandBuffer)` method records all passes without `begin()`/`end()`, enabling multiple RenderGraphs to record into the same CB
- **Lazy RenderGraph creation in solvers**: Solver RenderGraph is built on first `Step()` call and cached; subsequent frames only execute the cached graph
- **Cross-RenderGraph synchronization via `prev_access`**: Rendering RenderGraph declares initial buffer state on `ImportExternalResource` so barriers are correct even though physics and rendering passes live in separate `RenderGraph` objects
- **`ComplexRenderGraphBuilder` accepts optional physics model matrices**: New overload accepts `const ComputeBuffer*` for model matrices; the lit and shadow passes read from it when available
- **Physics example refactored** to use `ComplexRenderGraphBuilder` + `DummySolver` instead of the monolithic `PhysicsExampleRenderGraphBuilder`
- **BREAKING**: `PhysicsExampleRenderGraphBuilder` removed (was example-only, no downstream consumers)

## Capabilities

### New Capabilities

- `physics-solver-interface`: Abstract `ISolver` base class defining the solver lifecycle (`Step`, `IsInitialized`), solver registration API on `PhysicsSystem`, and the per-frame `PhysicsSystem::Step()` dispatch
- `physics-dummy-solver`: A minimal `DummySolver` that translates all rigid bodies along `-Z` by a configurable step size and updates model matrices via a compute shader; owns its own RenderGraph created lazily on first `Step()`
- `physics-render-graph-separation`: Physics solvers own independent `RenderGraph` instances; `RenderGraph` exposes a `RecordToCommandBuffer` method that records passes without `begin()`/`end()`, enabling a single `vk::CommandBuffer` to host passes from multiple RenderGraphs
- `render-graph-model-matrix-input`: `ComplexRenderGraphBuilder` can optionally accept an external model matrices buffer; when provided, shadow map and lit passes read model matrices from this buffer instead of relying only on per-draw push-constant matrices

### Modified Capabilities

- `physics-render-graph-handle-forwarding`: The pre-imported handle forwarding pattern (where `PhysicsSceneBufferHandles` is passed from solver to detectors) remains and now applies within the physics RenderGraph only; the rendering RenderGraph imports shared buffers independently with `prev_access` marking the state left by the physics graph

## Impact

- **`engine/Physics/Solver/`**: New `ISolver` base class header, new `DummySolver` (.h/.cpp), new dummy solver compute shader
- **`engine/Physics/PhysicsSystem`**: Adds `RegisterSolver()`, `Step()`, solver storage
- **`engine/Render/Pipeline/RenderGraph/RenderGraph`**: New `RecordToCommandBuffer()` method, `GetNumPasses()`
- **`engine/Render/Pipeline/RenderGraph/ComplexRenderGraphBuilder`**: New `BuildDefaultRenderGraph` overload accepting `const ComputeBuffer* model_matrices`
- **`engine/Render/RenderSystem/SceneDataManager`**: May need `SetModelMatricesBuffer` called from physics side (already exists, verify integration)
- **`engine/MainClass`**: `RunOneFrame()` calls `physics->Step()` between `StartFrame()` and render graph recording
- **`example/physics_example/`**: Replaced `PhysicsExampleRenderGraphBuilder` with `ComplexRenderGraphBuilder` + `DummySolver`
- **`engine/Physics/CMakeLists.txt`**: New shader compilation for dummy solver
