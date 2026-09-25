## 1. RHI: in-place storage replacement

- [x] 1.1 Remember the debug name in `DeviceBuffer`: store the `name` a buffer is created with on the buffer facade — not on `BufferAllocation`, which is destroyed and re-created on every reallocation — and pass it to `AllocatorState::AllocateBuffer` whenever the storage is replaced.
- [x] 1.2 Add the protected `DeviceBuffer::ReallocateStorage(const AllocatorState&, size_t)`: allocate the replacement from `allocation.GetMemoryType()` and the buffer's remembered name, then adopt it so the previous allocation takes the retirement path. Verify the build succeeds and a reallocated buffer reports the new size.
- [x] 1.3 Add `ComputeBuffer::Reallocate(allocator, bytes)` (exact, may shrink) and `ComputeBuffer::EnsureCapacity(allocator, bytes)` (grow-only, no-op at or below the current size). Verify a within-capacity `EnsureCapacity` call leaves the handle and contents unchanged.
- [x] 1.4 Document the invalidation contract on both entry points: the object address survives, `GetBuffer()` and `GetVMAddress()` do not, contents are not preserved. Verify the header text names all three.
- [x] 1.5 Add `test/unit/rhi/buffer_reallocation_test.cpp` using a standalone `DeviceInterface` + `AllocatorState` + `EpochTracker` (no `DeviceContext`), asserting: object address unchanged, handle changed, replaced allocation parked, VMA allocation count back to one after `ReleaseAllParked`, within-capacity request reallocates nothing. Verify `ctest` runs it green.
- [x] 1.6 Add a case to that test for an allocator with no retirement facility installed, asserting the replaced allocation is destroyed immediately. Verify it passes.

## 2. Convert the five `EnsureBuffer` copies to in-place replacement

- [x] 2.1 `engine/Physics/PhysicsScene.cpp`: convert the `unique_ptr` copy (27 buffers) to create-once-then-`Reallocate`. Verify a slot-count change leaves each buffer object's address unchanged (assert or log in a scratch run, then remove).
- [x] 2.2 `engine/Physics/Solver/XPBDGpuSolver.cpp`: convert `Impl::EnsureBuffer` and its 34 call sites, including the 24 reached through `EnsureReduceGroup`. Verify the XPBD headless tests still pass.
- [x] 2.3 `engine/Physics/Collision/SpatialHashBroadDetector.cpp`: convert `Impl::EnsureBuffer` (19 buffers) and move the three inline grow-only blocks (`gpu_global_list`, `gpu_global_count`, `gpu_unique_count`) onto `EnsureCapacity`. Verify the broad-phase tests still pass.
- [x] 2.4 `engine/Physics/Collision/ConvexCollisionDetector.cpp`: convert the five inlined blocks in `EnsureBuffers()`. Verify the narrow-phase tests still pass.
- [x] 2.5 Verify no resize path still constructs a replacement object: grep `engine/Physics` for `ComputeBuffer::CreateUnique` and confirm every remaining occurrence is on a first-creation branch only.

## 3. Model matrices ownership moves to the render side

- [x] 3.1 `SceneDataManager`: hold the buffer as `std::unique_ptr`, create it once in `Create()`, write identity matrices into it there, and add `EnsureModelMatricesCapacity(uint32_t element_count)` (element-count units, initial reservation, grow-only) plus a non-const `GetModelMatricesBuffer()` accessor. Verify a debug run shows binding 2 pointing at the buffer before any physics work.
- [x] 3.2 `SceneDataManager`: delete `SetModelMatricesBuffer` and the external-buffer member, and bind the descriptor directly from the owned buffer. Verify the header no longer declares a setter.
- [x] 3.3 Delete `ComputeBuffer::CreateShared`, the `std::shared_ptr<const Rhi::DeviceBuffer>` overload of `RenderGraphBuilder::ImportExternalResource`, and `RenderGraph2ExtraInfo::owned_external_buffers`. Verify a repo-wide grep for `CreateShared` and `owned_external_buffers` returns nothing.
- [x] 3.4 `ComplexRenderGraphBuilder::BuildDefaultRenderGraph`: drop the `model_matrices_buffer` parameter, import the render system's buffer unconditionally, and remove the `has_model_matrices` branch. Verify the default render graph still renders physics-driven bodies.
- [x] 3.5 `EditorRenderGraphBuilder::BuildEditorRenderGraph`: drop the parameter, import unconditionally, and remove its `has_model_matrices` branch. Verify the editor example renders.
- [x] 3.6 `PhysicsScene`: delete `m_gpu_model_matrices`, the `shared_ptr` `EnsureBuffer` overload, and the `model_matrices` field of `PhysicsGpuBuffers`; drop the corresponding line from `RefreshGpuBuffers` and `Clear`. Verify `PhysicsGpuBuffers` contains only owned, uploaded buffers.

## 4. Solver model matrix entry point

- [x] 4.1 `ISolver`: add `virtual void GPUCalcModelMatrices(vk::CommandBuffer cb, Rhi::ComputeBuffer &target) = 0` with `Rhi::ComputeBuffer` forward-declared, so every concrete solver implements it rather than inheriting a shared default, and document the caller-ensures-capacity contract (the implementation clamps its dispatch and asserts in debug builds). Verify `engine/Physics` headers still include no `Render/` or `Framework/` header.
- [x] 4.2 `PhysicsSystem`: add the scene-scoped `GPUCalcModelMatrices(PhysicsScene&, cb, target)` that forwards to that scene's solvers in registration order. Verify a unit test with two scenes shows only the named scene's solver is invoked.
- [x] 4.3 Move `engine/Physics/shader/solver/XPBDSolver/model_matrix.comp` to `engine/Physics/shader/solver/common/model_matrix.comp` and confirm the CMake pipeline picks it up with no CMake edit. Verify the SPIR-V appears at `<ENGINE_PHYSICS_SPIRV_DIR>/solver/common/model_matrix.comp.spv`.
- [x] 4.4 `XpbdGpuSolver`: remove the model matrix dispatch from the tail of `GPUStep` and implement `GPUCalcModelMatrices` with the shared shader, recording its own barrier and clamping the dispatch to the target's capacity (debug assert on a shortfall). Verify a step alone writes no model matrices.
- [x] 4.5 `DummySolver`: remove the `ModelMatrices` binding and the model matrix write from `dummy_solver.comp` (including its duplicated `quaternion_to_mat4`), and implement `GPUCalcModelMatrices` with the shared shader. Verify the dummy path renders bodies at their displaced poses.

## 5. Assembly-layer call sites

- [x] 5.1 `MainClass::RunOneFrame`: replace the forwarding call with ensure-capacity plus a model matrix production recorded after `physics->GPUStep(cb)` and before `RecordAllPasses`. Verify the capacity is ensured before `StartFrame` so the frame's descriptor handle is final.
- [x] 5.2 `PhysicsApp::Step`: produce model matrices on the step's command buffer after `GPUStep`, so a render frame following a step sees the new poses. Verify the app's offscreen test still shows moving bodies.
- [x] 5.3 `PhysicsApp::CommitScene`: wire the render-owned buffer and ensure its capacity before the initial production, replace the seed step with a direct production call, and drop the buffer argument from the graph build. Verify a paused windowed run shows bodies rather than only the skybox.
- [x] 5.4 `example/editor_run_game_example/main.cpp`: delete the one-shot graph rebuild and its `has_model_matrices_in_graph` flag, move the production call outside the `m_is_playing` gate, and drop the buffer argument from the graph build. Verify the editor renders while stopped and while playing.
- [x] 5.5 `example/external_resource_loading_example/main.cpp`: update the graph build call for the removed parameter. Verify it builds and renders.

## 6. Tests

- [x] 6.1 Rewrite `test/engine/headless/model_matrices_ownership_test.cpp` around stable identity: the buffer object's address is unchanged across a slot-count change, the graph resolves the current handle at record time, no `shared_ptr` participates, and a frame that produces nothing issues no model matrix dispatch. Verify the test passes under the validation layer.
- [x] 6.2 Add a paused-frame fixture asserting that a frame recorded with simulation disabled and no step still yields valid, non-degenerate matrices for a physics-driven renderer. Verify it fails if the production call is removed.
- [x] 6.3 Update `test/engine/headless/gpu_buffer_retirement_test.cpp` for the `ISolver` change and confirm it still passes.
- [x] 6.4 Run the physics app stability test and the long dynamic run, and confirm a flat memory profile across repeated grow/shrink of the body and shape counts.

## 7. Rebase the three in-flight changes

- [x] 7.1 `compute-kernel-dispatch`: remove the shared-ownership clause and its scenario from `specs/rhi-module/spec.md`, and update `specs/physics-gpu-shaders/spec.md` for the shared model matrix shader path.
- [x] 7.2 `descriptor-arena-epoch-buckets`: remove the same clause and scenario from `specs/rhi-module/spec.md`.
- [x] 7.3 `physics-step-simplification`: state that the three entry shaders must take an explicit body count instead of reading `rigid_body_alive.v.length()`; state that `detect_collisions.comp`'s write guard must stop doubling as the configured budget guard; add `GPUCalcModelMatrices` to its `ISolver` method list; restate `rhi-buffer-capacity` as a modification of this change's `rhi-buffer-reallocation` growth policy rather than a rival capability; update the `physics-gpu-shaders` path; and remove the model matrices forward carve-out from its `physics-main-loop-integration`, `editor-physics-pipeline` and `xpbd-solver-multi-rg` deltas.
- [x] 7.4 Verify `openspec validate` reports all three changes valid, and that no pending delta still mentions `CreateShared`, the shared-ownership factory, `SetModelMatricesBuffer`, or `solver/XPBDSolver/model_matrix.comp`.

## 8. Final verification

- [x] 8.1 Full build (both presets available in this environment) and the complete `ctest` suite pass.
- [x] 8.2 `openspec validate stable-buffer-identity --strict` passes.
- [x] 8.3 Repo-wide grep confirms no remaining `shared_ptr` to a compute buffer, no `owned_external_buffers`, no `CreateShared`, no `SetModelMatricesBuffer`, and no `model_matrices_buffer` parameter on either render graph builder.
- [x] 8.4 Run the windowed editor and the windowed physics app once and confirm physics-driven bodies render at the correct transforms in play, pause and stopped states.
