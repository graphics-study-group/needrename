## Why

`model_matrices` is the one GPU resource whose lifetime spans two subsystems: physics produces it, render reads it, and the render graph is built once while the buffer may be replaced at any time. The archived `gpu-buffer-retirement` change made it *stably owned* by adding `ComputeBuffer::CreateShared` and letting the render side hold a share of the reference. That treated the symptom by introducing shared ownership, and it made `model_matrices` the only reference-counted buffer in an engine whose every other buffer is uniquely owned and passed as a raw pointer.

It also did not make the graph reference the *right* buffer. The graph pins whatever it imported at build time, while `SceneDataManager` rebinds descriptor binding 2 to the current buffer on every frame — so after any rigid-body slot-count change the two diverge, and the graph's share keeps alive a buffer the renderer no longer uses. The pointer it protects is not even dereferenced today: the render graph's buffer-barrier path collapses to a global `MemoryBarrier2`, so the shared reference currently guards a pointer that nothing reads.

The root cause is not ownership but **identity churn**. `EnsureBuffer` replaces the buffer *object* whenever the requested size differs, so every long-lived reference to a buffer facade — the render graph's `buffer_mapping`, `SceneDataManager`'s pointer — is a dangling pointer waiting for a resize. Buffer allocations are already retire-safe (a replaced allocation is parked under a submission epoch), so the storage can be replaced *in place*; the facade does not have to move.

This change therefore makes buffer objects stable and gives the model matrices buffer a single owner in the same subsystem as its long-lived reader. Two further consequences of the current design disappear with it: the editor must rebuild its entire render graph (and re-point both widget textures) once the physics buffer first appears, and model matrices can only be produced as a side effect of a full physics step — which is why the physics app has to run a whole step with simulation disabled just to seed them.

## What Changes

- **New RHI facility: in-place storage replacement.** `DeviceBuffer` gains a protected reallocation primitive that swaps the underlying `BufferAllocation` while the buffer object stays at the same address; `ComputeBuffer` exposes it as two entry points — an exact `Reallocate(allocator, bytes)` (may grow or shrink, used where the current exact-size semantics must be preserved) and a grow-only `EnsureCapacity(allocator, bytes)` (used by the render-owned model matrices buffer). The replaced allocation takes the existing retirement path unchanged.
- **The invalidation contract is explicit.** Reallocation preserves the object address and discards the contents; `GetBuffer()` and `GetVMAddress()` obtained before it are invalid, exactly as `std::vector` iterators are. Only a buffer whose descriptor bindings are re-established on every use may expose reallocation: `ComputeBuffer` does, `IndexedBuffer` (write-once descriptors, cached mapped pointer, slice geometry) does not.
- **The debug name follows the buffer.** `DeviceBuffer` remembers the name it was created with and uses it for every replacement storage, so a reallocation reproduces the name and the caller never repeats it. The name deliberately lives on the facade, which is created once, rather than on the allocation, which is destroyed and re-created on every reallocation.
- **All five `EnsureBuffer` copies convert to in-place replacement** (~85 buffers across `PhysicsScene`, `XpbdGpuSolver`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`). Each buffer object is created once and never replaced again.
- **BREAKING: `model_matrices` becomes a render-owned resource.** `SceneDataManager` owns it as a `unique_ptr`, creates it once, sizes it on demand (`EnsureModelMatricesCapacity`), and never replaces the object. The render graph unconditionally imports it, so the `has_model_matrices` build-time branch, the builder parameter, and the editor's one-shot graph rebuild all disappear.
- **BREAKING: the shared-ownership vocabulary is deleted.** `ComputeBuffer::CreateShared`, `RenderGraphBuilder`'s `shared_ptr` overload, `RenderGraph2ExtraInfo::owned_external_buffers`, `SceneDataManager::SetModelMatricesBuffer` and `PhysicsScene::PhysicsGpuBuffers::model_matrices` are removed.
- **New solver entry point: `ISolver::GPUCalcModelMatrices(cb, target)`**, with `PhysicsSystem::GPUCalcModelMatrices(PhysicsScene&, cb, target)` forwarding per scene. Model matrices are produced by an explicit call in a caller-provided buffer rather than as the last phase of `GPUStep`, so a caller that has no render target simply does not call it, and a caller that only wants matrices (the physics app's commit-time seed) does not have to run a step.
- **A frame rule replaces three accidental guarantees.** Model matrices are produced once per frame, between `BeginMainCommandBuffer` and the render graph's recording, independent of play, pause and simulation state. Today the main loop relies on `GPUStep` being unconditional, the editor relies on `IsPhysicsActive` keeping `model_mat_index` negative while stopped, and the physics app relies on a commit-time seed — three unrelated facts, of which the editor's guarantees only that nothing reads an uninitialized buffer.
- **The buffer is initialized once at creation** to identity matrices, so a missed production turns into a visibly wrong transform rather than invisible bodies (the failure mode `physics-app-body-state-write` already shipped once).
- **The model-matrix shader becomes shared.** `engine/Physics/shader/solver/XPBDSolver/model_matrix.comp` moves to a shared location under the solver group and is used by both the XPBD and dummy solvers; `dummy_solver.comp` stops writing model matrices.
- **Three in-flight changes are rebased** so their pending deltas do not re-introduce what this change removes: the shared-ownership factory clause in three `rhi-module` deltas, the `model_matrix.comp` path in two `physics-gpu-shaders` deltas, and the capacity-policy overlap, `ISolver` method list and `SetModelMatricesBuffer` text in `physics-step-simplification`.
- No change to the retirement protocol, the descriptor arena, the compute dispatch surface, or the barrier placement of any existing pass.

## Capabilities

### New Capabilities

- `rhi-buffer-reallocation`: the contract for replacing a device buffer's storage in place — object identity is preserved, the previous handle and mapped pointer are invalidated, contents are not preserved, the debug name is preserved by the buffer itself, and the capability is exposed only by buffer types whose descriptor bindings are re-established on every use.

### Modified Capabilities

- `rhi-module`: the shared-ownership buffer factory clause and its scenario are removed from the type inventory.
- `render-graph-model-matrix-input`: both requirements are replaced — the render graph unconditionally imports the render-owned buffer, and `SceneDataManager` owns it instead of receiving a forwarded reference.
- `editor-physics-pipeline`: the editor builder no longer takes a model matrices parameter, and the editor loop produces model matrices every frame rather than only while playing.
- `physics-main-loop-integration`: the model matrices forwarding requirement is removed and the frame order gains the production step.
- `physics-solver-interface`: `ISolver` gains `GPUCalcModelMatrices`, and `PhysicsSystem` gains the scene-scoped entry point that forwards it.
- `physics-dummy-solver`: `DummySolver` implements the new entry point and its shader no longer writes model matrices.
- `physics-gpu-shaders`: the model matrix shader's source path moves to a shared location.
- `xpbd-solver-multi-rg`: `GPUStep` no longer notifies `SceneDataManager`, and the model matrix pass is no longer its final phase.
- `physics-app`: the commit sequence loses the forwarding step and the graph build loses its buffer argument.
- `physics-app-pause`: the commit-time seed is restated as a direct model-matrix call rather than a step run with simulation disabled.
- `physics-render-graph-separation`: the external-resource requirement's model matrices case is restated, since the buffer is no longer written by a preceding physics render graph.
- `com-descriptors`: the `SyncGpuBuffers` requirement's buffer set no longer includes model matrices.

## Impact

**Code**
- `engine/Rhi/Buffer/` — `DeviceBuffer` reallocation primitive and the debug name it remembers, `ComputeBuffer` entry points, `ComputeBuffer::CreateShared` removed.
- `engine/Rhi/Device/` — no change to `BufferAllocation`: the debug name stays out of the allocation, which is replaced on every reallocation.
- `engine/Rhi/Submission/` — no protocol change; the replaced allocation takes the existing upper-bound retirement path.
- `engine/Render/Pipeline/RenderGraph/` — `RenderGraphBuilder`'s `shared_ptr` overload and `owned_external_buffers` removed.
- `engine/Render/RenderSystem/SceneDataManager.*` — ownership, demand sizing and one-time initialization of the model matrices buffer.
- `engine/Framework/Tools/ComplexRenderGraphBuilder.*`, `app/editor/Editor/Render/EditorRenderGraphBuilder.*` — parameter removed, unconditional import.
- `engine/Physics/` — `ISolver`, `PhysicsSystem`, `PhysicsScene`, `XpbdGpuSolver`, `DummySolver`, and the four `EnsureBuffer` copies; the model matrix shader moves.
- `engine/Framework/MainClass.cpp`, `app/physics/PhysicsApp.cpp`, `example/editor_run_game_example/main.cpp`, `example/external_resource_loading_example/main.cpp` — seven call sites.

**API**
- Breaking: `ComputeBuffer::CreateShared`, `RenderGraphBuilder::ImportExternalResource(std::shared_ptr<const Rhi::DeviceBuffer>)`, `SceneDataManager::SetModelMatricesBuffer`, `PhysicsScene::PhysicsGpuBuffers::model_matrices`, and the `model_matrices_buffer` parameter of both render graph builders.
- Additive: `ComputeBuffer::Reallocate` / `EnsureCapacity`, `ISolver::GPUCalcModelMatrices` (pure virtual), `PhysicsSystem::GPUCalcModelMatrices`, `SceneDataManager::EnsureModelMatricesCapacity`.

**Dependencies**
- Requires the archived `gpu-buffer-retirement` change (in-place replacement is safe only because the replaced allocation is parked under an epoch).
- Must land before `descriptor-arena-epoch-buckets`, `compute-kernel-dispatch` and `physics-step-simplification`: it deletes vocabulary those changes' pending deltas currently restate, and it establishes the reallocation contract whose growth policy `physics-step-simplification` extends.

**Tests**
- A standalone RHI test for in-place replacement (address stability, handle change, retirement of the replaced allocation, name preservation, no reallocation within capacity).
- The model matrices ownership test is rewritten around stable identity and per-frame production, including that a frame with no target produces no extra dispatch.
- A paused-frame fixture must show physics-driven renderers keep valid matrices without any physics step.
