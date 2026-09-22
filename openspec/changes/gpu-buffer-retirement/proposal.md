## Why

The RHI has no notion of GPU work that is in flight. `BufferAllocation`'s destructor calls `vmaDestroyBuffer` immediately (`MemoryAllocation.cpp:57-74`) and no retirement queue exists anywhere in `engine/Rhi/`, while the engine keeps two to three frames in flight. Every buffer resize in the physics pipeline therefore destroys a `VkBuffer` that an in-flight command buffer may still be reading:

- `MainClass::RunOneFrame` runs `PreGPUStep()` at `:291` while frames N-1 and N-2 are still executing on the GPU (`StartFrame` at `:283` only waits the fence of the frame that used the same in-flight slot).
- All five `EnsureBuffer` copies destroy and recreate on any size change, and `RadixSort::Record` even rebuilds an inner `ParallelScan` mid-recording.
- `PostGPUStep` runs right after submission, so any readback reads before the GPU has written.

The same missing concept makes a second class of bug reachable: the render side borrows physics-owned buffers as raw pointers (`SceneDataManager` stores `const ComputeBuffer *`; `RenderGraphBuilder::ImportExternalResource` stores `&buffer`) which dangle as soon as physics resizes the buffer after the graph was built.

This change introduces the missing concept — the submission epoch — and makes destruction retire-safe *at the allocation layer*, so none of the ~92 physics buffers or 261 binding call sites has to change.

## What Changes

- **New RHI facility: submission epochs and a retirement queue.** A monotonic watermark identifies a completion promise; retired resources are parked under the newest outstanding watermark and freed when completion is reported in order.
- **Allocation-layer cut (no buffer API change).** `BufferAllocation` gains an optional retire sink; its destructor hands the allocation to the retirement queue instead of destroying it. `ComputeBuffer`, `DeviceBuffer`, `IndexedBuffer`, all five `EnsureBuffer` copies and every algorithm contract are untouched.
- `AllocatorState` gains a retire-sink setter, keeping its pinned `AllocatorState(DeviceInterface &)` constructor intact.
- **Drivers.** `FrameManager` opens exactly one epoch per frame (at the end of a successful `StartFrame`) and reports it complete after the per-frame fence wait; `SubmissionHelper` opens and completes one epoch per submission. Every `device.waitIdle()` site also drains, and `AbortEpoch()` covers error paths.
- **BREAKING:** physics `model_matrices` becomes a stably owned buffer (`ComputeBuffer::CreateShared`) and the render side holds that stable reference instead of a borrowed raw pointer. `SceneDataManager::SetModelMatricesBuffer` and `ComplexRenderGraphBuilder::BuildDefaultRenderGraph` change accordingly.
- No compute dispatch API, shader, or descriptor-set behavior changes in this change.

## Capabilities

### New Capabilities

- `rhi-gpu-resource-retirement`: the submission-epoch contract, the retirement queue's release rules (the release-time watermark, in-order completion, the single immediate-release condition), the submitter protocol (open / report / abandon / release-on-idle), and the guarantee that a resource is never released while a submission that may reference it is outstanding.

### Modified Capabilities

- `submission-helper-sync`: `SubmissionHelper` becomes a retirement driver — it depends on the tracker for construction, opens and reports one epoch per submission, and reports completion before releasing its batch staging. The "no synchronization primitives" clause is clarified to exclude a completion ledger.
- `rhi-module`: the module inventory gains the epoch tracker, the allocation retire sink, and the shared-ownership buffer factory.
- `rhi-directory-structure`: the tracker and the retire sink are assigned to their subdirectories.
- `render-graph-model-matrix-input`: the model matrices buffer is forwarded to `SceneDataManager` and imported by the graph builder as a stably owned buffer rather than a borrowed raw pointer.
- `physics-main-loop-integration`: the model matrices forwarding requirement gains the stable-ownership clause (the other requirements in that file belong to the later `physics-step-simplification` change).
- `editor-physics-pipeline`: `EditorRenderGraphBuilder`'s model matrices parameter becomes a stably owned reference (the two lifecycle requirements in that file belong to the later `physics-step-simplification` change).

## Impact

**Code**
- `engine/Rhi/Submission/` — new epoch tracker; `SubmissionHelper` gains epoch open/report.
- `engine/Rhi/Buffer/` — `BufferAllocation` retire sink and `ComputeBuffer::CreateShared`.
- `engine/Rhi/Device/` — `AllocatorState` retire-sink wiring; `DeviceContext` owns the tracker.
- `engine/Render/RenderSystem/FrameManager.*` — per-frame epoch open/report and drain.
- `engine/Render/RenderSystem/SceneDataManager.*`, `engine/Render/Pipeline/RenderGraph/RenderGraphBuilder.*` — stable buffer ownership for `model_matrices`.
- `engine/Framework/MainClass.cpp`, `engine/Framework/Tools/ComplexRenderGraphBuilder.*`, `app/editor/Editor/Render/EditorRenderGraphBuilder.*`, `app/physics/PhysicsApp.cpp` — call-site updates for the ownership change.

**API**
- Breaking: the `model_matrices` forwarding signature. Everything else is additive.

**Tests**
- Headless fixtures that change geometry between steps must run under the validation layer without use-after-free; a long-running dynamic fixture must show a non-monotonic retired-resource count.
