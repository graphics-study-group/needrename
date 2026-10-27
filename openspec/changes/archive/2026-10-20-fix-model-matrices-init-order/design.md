## Context

See `proposal.md` — Why for the user-visible failure. This section records the mechanism and the constraints that make only one fix shape work.

Two writes reach the model matrices buffer before the first frame:

1. **The identity fill.** `SceneDataManager::Create` (`engine/Render/RenderSystem/SceneDataManager.cpp:169-175`) enqueues `MAX_MODEL_MATRICES` identity matrices through the frame manager's `SubmissionHelper`. The helper does not record anything at enqueue time: `EnqueueBufferSubmission` stages the data and pushes a lambda (`engine/Rhi/Submission/SubmissionHelper.cpp:223-271`); the copy is recorded into a one-time command buffer inside `ExecuteSubmission` (`SubmissionHelper.cpp:398-455`). The frame manager's only submission point is `SubmitFrame`, i.e. the end of a rendered frame (`FrameManager.cpp:291`), and that batch's signal is waited on by the frame's main command buffer.
2. **The commit-time seed.** `PhysicsApp::CommitScene` records `PhysicsSystem::GPUCalcModelMatrices` into its own command buffer, submits it directly and waits on its own fence (`app/physics/PhysicsApp.cpp:753-779`).

The queue therefore sees the seed first and the identity copy later:

```
 CommitScene                    first RenderNextFrame
     |                                  |
     |  seed CB (submit + wait)         |
     |  => buffer = initial poses       |
     |                                  +-- SubmitFrame: staged batch (identity copy) --> main CB waits
     |                                  |   => buffer = identity
     v                                  v
 [seed] ----------------------------> [identity copy] -> [main CB draws with identity]
```

Three facts explain why the fill survives that long and why it lands late:

- `PhysicsScene::SyncGpuBuffers` (`engine/Physics/PhysicsScene.cpp:240-243`) flushes through the **PhysicsAdaptor's own** `SubmissionHelper` (`engine/Framework/Bridge/PhysicsAdaptor.cpp:316-323`), not the frame manager's. The commit sequence's immediate flush therefore does not retire the frame manager's batch.
- The frame manager's helper is created in `FrameManager::Create` and nothing submits it before the first `SubmitFrame`; at render-system creation its state is `Reset`.
- The fill is the first operation enqueued in the process: `RenderSystem::Create` creates the frame manager and then the scene data manager (`engine/Render/RenderSystem.cpp:109-110`), and the default light-map clear is enqueued *after* the fill (`SceneDataManager.cpp:193`).

## Goals / Non-Goals

**Goals:**

- The buffer's initialization is complete on the device before `SceneDataManager::Create` returns, so no producer can be followed by a pending creation-time write.
- `PhysicsApp` keeps exactly one model-matrix production per transition: one at `CommitScene`, one per `Step`, none per paused frame.
- The ordering assumption the seed depends on is stated where a future editor of that code will read it.
- A headless test pins both halves of the contract (the fill exists and is visible; a producer before the first frame is not overwritten).

**Non-Goals:**

- Preserving buffer contents across a storage reallocation. A `std::vector`-style copy-on-grow is a future reallocation mechanism; this change deliberately does not introduce partial copies.
- Per-frame model-matrix production in `PhysicsApp`, and removing `Step`'s production.
- The pause model, and the stale `physics-app` requirement that still claims `Pause`/`Resume` wrap `PhysicsScene::SetSimulationEnabled`.
- Computing model matrices from scene state at creation. At `SceneDataManager::Create` no scene exists yet (`MainClass::Initialize` creates the render system at `MainClass.cpp:145`, before any project or GameObject), and the fill size is the initial reservation, not a body count.

## Decisions

### D1: Fix the initialization write, not the producer

The invariant "a resource is initialized before it is published to producers" belongs to the buffer's owner. `PhysicsApp` cannot see the render system's pending uploads, and putting a flush there would make every host responsible for the engine's batching.

*Alternative rejected:* flush the frame manager's staged uploads from `PhysicsApp::CommitScene` before the seed. It is one line and fixes today's symptom, but it leaks the render system's upload queue into the app and leaves the class of bug intact — any later enqueue to this buffer, or any other host producing before its first frame, reproduces it.

### D2: Complete the fill with `ExecuteSubmissionImmediately()` inside `Create`

Immediately after the fill's `EnqueueBufferSubmission`, `SceneDataManager::Create` calls `ExecuteSubmissionImmediately()` on the same helper. At that point the fill is the only pending operation (see Context), so the call is effectively a dedicated submission: it records the copy into a one-time command buffer, submits it, waits on the fence, reports the submission's epoch and releases the staging — all through the path the helper already owns.

*Alternatives rejected:*

- *Hand-rolled one-time command buffer + staging + fence inside `SceneDataManager`* (the shape the commit-time seed uses). Self-contained and free of helper state, but duplicates the copy/barrier plumbing and the retirement bookkeeping for no behavioural difference.
- *Removing the fill.* It is the "nothing produced reads a wrong transform rather than undefined memory" guarantee required by `render-graph-model-matrix-input`; the paused-frame symptom would become undefined data instead of identity.

*Precondition:* the helper must be in `Reset` when `Create` runs. It is, because no `ExecuteSubmission` happens before the first `SubmitFrame`. This is forward-safe in the useful direction: if a future initialization path submits a deferred batch first, `ExecuteSubmissionImmediately()` throws instead of silently reintroducing the bug.

### D3: Keep the commit-time seed; add no per-frame production

Production counts stay: one at `CommitScene`, one per `Step`, none per paused frame.

*Alternatives rejected:*

- *Produce in `RenderNextFrame` every frame.* While running, `Step` already produces, so this doubles the dispatch the moment stepping begins; while paused it adds a producer whose only job is to overwrite the fill that this change makes correct in the first place.
- *Move the sole production into the first rendered frame with a freshness flag.* It satisfies "produce once" only by adding a piece of state and a rule ("a frame produces only if nothing produced since the last pose change") that readers must hold in their heads, and it changes the `physics-app` requirement that `Step` refreshes the buffer.

### D4: State the ordering contract in three places

The seed's correctness depends on a fact that is not local to it, so it is recorded at each level that can break it:

- the requirement in `specs/render-graph-model-matrix-input` (the delta), which makes completion-before-`Create`-returns normative;
- `SceneDataManager::Create`'s documentation, where hosts read the contract;
- a comment at the seed site in `PhysicsApp::CommitScene`, warning that no engine-side write to this buffer may be pending after render-system creation.

### D5: Regression test at the engine level

The failure is an ordering property of the render system, so the test reproduces it there: after render-system creation, (a) read the buffer back and assert identity, then (b) produce model matrices into it without submitting a frame, flush the frame manager's staged uploads, read it back and assert the producer's values. Reading back requires a copy into a staging buffer, because the model matrices buffer is device-local.

*Alternatives rejected:* an app-level pixel comparison between the paused first frame and a later paused frame. It is closer to the reported symptom, but it depends on frame determinism, camera projection and pixel layout, and it would fail for reasons unrelated to this contract.

*Fidelity note:* the test flushes with `ExecuteSubmissionImmediately`, while the frame path uses `ExecuteSubmission`. Both record the same pending operations into a one-time command buffer; only the wait mechanism differs (fence versus timeline semaphore). The test's header comment records that equivalence.

### D6: Model-matrix computation stays on the GPU

The pose-to-matrix mapping lives in exactly one place, the shared `model_matrix.comp` (`physics-dummy-solver` requires it), and `PhysicsSystem::GPUCalcModelMatrices` forwards per scene to every registered solver. A CPU computation would duplicate the mapping and would not address the ordering property at all: a CPU upload submitted through the same deferred path races identically.

## Risks / Trade-offs

- **[One startup stall on a fence]** → It happens once, before any frame is submitted, and `PhysicsApp`'s CPU does not overlap GPU work at initialization anyway. Steady-state cost is zero.
- **[A future initialization path submits a deferred batch before `Create`]** → `ExecuteSubmissionImmediately` throws on a non-`Reset` state; the failure is loud instead of a silent ordering regression. The precondition is documented at the call site.
- **[Contents are lost when the storage grows]** → Unchanged by this change. The scene freezes at `CommitScene`, so a growth between the seed and the first `Step` is unreachable today; a copy-on-grow mechanism is recorded as future work.
- **[The regression test uses the same API the fix uses]** → Not tautological: before the fix the pending fill is recorded into the test's flush and overwrites the producer; after the fix the helper is empty at that point and nothing is submitted.
- **[Other hosts rely on their own production cadence]** → Unaffected. `MainClass::RunOneFrame` and the editor produce inside the frame's command buffer, which already runs after any staged submission; this change only moves the initialization earlier.

## Migration Plan

Not applicable: no persisted state, no API changes, no data migration. The change is two hunks in `SceneDataManager::Create`, one comment in `PhysicsApp::CommitScene`, one documentation paragraph, and one new test; reverting it restores the previous ordering.
