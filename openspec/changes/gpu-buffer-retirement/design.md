# Design — gpu-buffer-retirement

See `proposal.md` — Why for the motivation. This document records the decisions that shape the mechanism, the traps it must avoid, and what is deliberately left out.

## Context

### Current state

Three facts from the existing code determine the whole design:

1. **Buffer destruction is immediate.** `DeviceBuffer::~DeviceBuffer() = default` over a `BufferAllocation` member; `~BufferAllocation` → `Destroy()` → `vmaUnmapMemory` + `vmaDestroyBuffer` (`MemoryAllocation.cpp:57-74`). There is no retirement queue, fence check, or deferred deletion anywhere in `engine/Rhi/`.
2. **The frame loop keeps two to three frames in flight on the CPU side.** `FrameManager::StartFrame` waits `command_executed_fences[fif]` (`FrameManager.cpp:211-215`) — the fence of the frame that used the same in-flight slot, i.e. three frames earlier — then `MainClass::RunOneFrame` runs `PreGPUStep()` at `:291` before the new frame is recorded. At that moment frames N-1 and N-2 may still be executing.
3. **A blocking submission proves only itself.** `SubmissionHelper::ExecuteSubmissionImmediately` submits and waits on its own fence (`SubmissionHelper.cpp:418-464`). It says nothing about earlier submissions. `MainClass.cpp:258` runs `PhysicsAdaptor::Flush` → `PhysicsScene::SyncGpuBuffers` (which resizes up to thirty buffers via `EnsureBuffer`) through exactly this path, before `StartFrame`.

### Existing ad-hoc lifetime mechanisms

Two hand-rolled mechanisms already cover one resource family each, with their own vocabulary:

- `RendererManager::Unregister` sets a per-renderer `pending_deallocation_countdown = FrameManager::FRAMES_IN_FLIGHT` (`RendererManager.cpp:88`) and `PerformPendingCleanUp` decrements it. That is a *ring countdown*: correct for the render path, but it can only express "N frames", never "the blocking upload submission that happens before the frame" — which is exactly the epoch the flush path needs.
- `SubmissionHelper` retains each batch's staging in `m_pending_staging` / `m_active_staging` and releases it after the batch fence is waited (`SubmissionHelper.cpp:162-165`, `:477`). That is a single-resource-family version of the retirement queue.

Buffer allocations had neither, which is why the resize path is the unprotected one. This design generalises the third case, and gives both existing mechanisms a common vocabulary they can migrate onto later (see Open Questions).

### Frame timeline (unchanged by this design, shown because the epoch protocol is defined against it)

```
MainClass::RunOneFrame
  :252-258  FlushPhysics -> SyncGpuBuffers -> ExecuteSubmissionImmediately   [epoch A]
  :283      StartFrame          waitForFences(command_executed_fences[fif])  <- frame N-3 done
  :291      PreGPUStep                                                       <- may resize buffers
  :293      BeginMainCommandBuffer
  :294-297  GPUStep(cb) + RecordAllPasses(cb)
  :301      CompleteFrame -> SubmitFrame
                ExecuteSubmission(timepoint 2)        <- staged upload submit
                submit2(main CB + copy CB), signal timepoint 4 @ eAllCommands
                                            , wait prev frame timepoint 4 @ eAllCommands
                                                                            [epoch B]
  :307      PostGPUStep
```

`FrameManager::SubmitFrame` issues two submissions per frame but attaches `command_executed_fences[fif]` to the main one, and the main batch waits on the staged upload's timepoint 2. Therefore one completion signal covers the whole frame.

### Constraint from existing specs

`rhi-compute-resource-binding` forbids presentation vocabulary in RHI's public API. The facility introduced here is therefore expressed in terms of *epochs and watermarks*, never frames.

### Inter-frame serialization is an existing invariant

`FrameManager::SubmitFrame` makes each frame's batch wait on the previous frame's timeline at its expected timepoints with `eAllCommands` (`FrameManager.cpp:281-284`), so **frame N's commands cannot begin before frame N-1's commands have finished**. Two consequences matter here:

- Physics' cross-frame dependency (frame N reads and writes the same buffers frame N-1 did) is already satisfied by this wait, so this design adds nothing for it.
- That dependency is satisfied *incidentally*, by the render side, and the same wait is what serialises the whole GPU pipeline. It is therefore a natural future optimisation target, and removing it would silently break physics — no error, just wrong simulation results.

The invariant is recorded here as a precondition this design relies on and does not change: **inter-frame GPU serialization is provided by the frame submission's previous-frame wait and MUST NOT be removed silently.** Work that genuinely needs to run independently of the frame must go to a separate queue with its own epochs and its own completion reporting — the submitter protocol in this change is submitter-agnostic and supports that — rather than by relaxing the wait.

## Goals / Non-Goals

**Goals**

- Make every buffer destruction safe by construction, without changing buffer APIs and without touching the ~92 physics buffers or the 261 `srb.BindBuffer` call sites.
- Keep the facility free of presentation concepts so both `Render` and `Physics` can drive it.
- Make the mechanism the single place that answers "is this resource still referenced by submitted work?".

**Non-Goals (design-level)**

- No automatic barriers; barrier placement stays manual and out of scope for this change.
- No change to descriptor sets, pools, or binding; that is the follow-up `descriptor-arena-epoch-buckets` change.
- No slot reuse or physics delete support; buffer sizes still follow the slot high-water mark.
- No aggressive (one-frame) reclamation, and no extraction of `FrameManager`'s timeline bookkeeping.

## Decisions

### D1: An epoch is a completion promise, not a command buffer and not a submission

A watermark id plus a "when is it done" reporting point. Command buffers, `vkQueueSubmit2` calls, and presentation frames are all the wrong granularity:

- One frame issues **two** submissions but has **one** completion signal; counting submissions over-counts.
- `ExecuteSubmission` with an empty batch issues **no** submission at all and advances the signal CPU-side; counting submissions under-counts.

The watermark is issued by the tracker and is never handed to Vulkan. GPU-side ordering continues to use the existing timeline semaphores; the two id spaces are linked only by "the submitter reports watermark w complete after observing signal s".

**Alternative rejected:** reuse timeline semaphore values as watermarks. Each in-flight slot owns its own timeline semaphore with independent value ranges, so values are not globally comparable.

### D2: A retired allocation parks under the *newest outstanding* watermark

The parking tag must be the maximum watermark issued and not yet completed — not the retiring submitter's own epoch. Rationale, straight from the code: `ExecuteSubmissionImmediately` proves only its own submission, yet `MainClass.cpp:258` retires up to thirty buffers through that path while frames N-1 and N-2 are still running. Tagging with "my own epoch" would classify those buffers as immediately free and reintroduce the use-after-free.

The cost is bounded: at most one in-flight depth of extra retention.

### D3: Completion advances only through the contiguous prefix

`Complete(w)` records w as completed, then advances the completed watermark through the contiguous run of completed watermarks. Without this, the blocking submission at `MainClass.cpp:258` — which is the *earliest* to be confirmed complete on the CPU — would advance the completed watermark past an earlier frame's still-outstanding epoch and free its buffers early.

**Observation that makes this cheap:** the GPU is already serialised across frames (`FrameManager.cpp:281-284` waits the previous frame's timepoint 4 at `eAllCommands`), and all submissions go to one queue in submission order, so completions are effectively in order in practice. The contiguous-prefix rule is insurance, not a throughput cost — it will normally advance all the way in one step.

### D4: One immediate-release rule; the other one was unsound and is removed

```
Retire(allocation):
    if (parking watermark <= completed watermark)   -> release immediately
    else                                            -> park under the release-time watermark
```

The parking watermark is **the watermark in effect at the moment of release** — the newest outstanding epoch watermark. It is not a function of when the allocation was created, and it is not a function of when the allocation was last bound.

**Why release-time is the right quantity.** The epochs that can reference an allocation are exactly those at or below its release watermark: an epoch whose recording begins after the release cannot bind it, because binding a released allocation is not a legitimate operation. Waiting for `completed >= release watermark` therefore covers exactly the right set, by the contiguous-prefix rule of D3. Note what the argument does *not* rely on: it does not require epochs to be non-nested — the blocking path can open an epoch inside a frame's epoch — only on "released implies no later legitimate binding".

**Why the creation-time rule was removed.** An earlier draft released an allocation immediately when it had been created during the current epoch and no submission had yet been issued for that epoch. That rule is **unsound**, because recording is not submission:

```
  allocate A -> bind A into a dispatch recorded into the frame's command buffer -> release A
  A is referenced by a command buffer that is recorded but NOT yet submitted
  releasing A frees the VkBuffer; the eventual submission then references destroyed memory
```

"Not yet submitted" was mistaken for "not yet referenced" — but the whole point of recording a dispatch is that it will be submitted. The rule's original motivation is real (per-call temporaries must not accumulate for a whole epoch), but the answer is the usage rule rather than early release: hot loops reuse a buffer they already own, and a per-call temporary legitimately stays alive until its epoch completes. That usage rule is already the chosen pattern (instance-held scratch with grow-only capacity).

**The submission notification is removed with it.** That protocol step existed only so the facility could answer "has anything been submitted in this epoch", which only the removed rule needed; the tracker cannot observe a `vkQueueSubmit2` by itself. With the rule gone the event has no consumer, and the protocol is back to four steps. The follow-up `descriptor-arena-epoch-buckets` change does not need it either — see that change's D3, which replaces per-submission sealing with a release-time watermark on each cache entry.

**Why the surviving rule matters.** A submitter that has already waited its fence reports completion and *then* releases staging, so the parking watermark is already complete and the release is immediate (see D9). Without it, texture-upload staging — which holds whole images — would be retained for up to a full in-flight depth. It also makes the retire/report order irrelevant: releasing before the report parks the allocation and the report drains it; releasing after the report frees it immediately. Both orders are correct.

**A tightening that is deliberately not taken.** Tracking the epoch in which each allocation was *last bound* would let an allocation released long after its last use be freed sooner. It is not needed for correctness, it would require a stamp on every binding path, and it takes the release decision away from the caller. The retention it would save is bounded by the in-flight depth, and the case only arises when a buffer is replaced — a geometry change, where the memory is being replaced anyway.

### D5: The cut goes in `BufferAllocation`, not in `ComputeBuffer`

| Option | Change surface | Verdict |
|---|---|---|
| **A. `BufferAllocation` holds an optional retire sink** | 92 buffer declarations, 5 `EnsureBuffer` copies, and every algorithm contract change **not at all**; every existing buffer becomes retire-safe at once | **Chosen** |
| B. New subclass (e.g. a managed compute buffer) | ~92 declarations, 5 `EnsureBuffer` copies, `RadixSortBuffers`-style contracts, and coexistence with the existing `unique_ptr<ComputeBuffer>` signatures | Rejected: same guarantee as A for a much larger diff |
| C. Outer wrapper handle | Same diff as B plus conversions back to `DeviceBuffer &` for every binding call | Rejected |

`BufferAllocation` is the object that actually holds the `VkBuffer` and VMA allocation, so it is the last place that can decide whether freeing now is safe. `ComputeBuffer` / `DeviceBuffer` / `IndexedBuffer` are facades over it and need no change. Dependency direction is preserved: the sink interface lives on the `Buffer` side and the tracker implements it, so `Buffer/` does not depend on `Submission/`.

### D6: The tracker is standalone-constructible; `DeviceContext` owns one and wires the sink

Two setups exist in the repo and both must keep working:

- **`DeviceContext`** (what `RenderSystem` and the physics system use) owns the tracker, installs it as the allocator's retire sink, and is the single place that guarantees the tracker outlives the allocator and the device.
- **Standalone headless setups.** `test/unit/rhi/rhi_standalone_test.cpp` and `test/unit/rhi/submission_helper_test.cpp` construct a `DeviceInterface` and an `AllocatorState` directly with no `DeviceContext`, and `rhi-module` pins that this is supported. The tracker SHALL therefore be constructible from those same facilities and installable through the same setter, so a standalone program can opt into retirement. Without a tracker installed, no sink is set and allocations destroy immediately — exactly today's semantics, which is the correct fallback for a setup that never defers work.

`AllocatorState` gains a **setter** rather than a constructor parameter, because `AllocatorState(DeviceInterface &)` is pinned by `rhi-module` and `gpu-context-module`; a setter leaves both intact.

`SubmissionHelper` takes the tracker as an explicit construction dependency (see the spec delta) rather than reaching it through `AllocatorState`, so that a standalone setup that never installs a sink still cannot construct a submitter that reports into nothing.

**A device has exactly one tracker.** Two trackers on one device are a programming error, not a safety hole: watermarks are self-issued, so a mismatched pair would park resources under watermarks that never complete — a leak, never a dangling reference. It is still worth catching, so the tracker SHALL assert in debug builds that it is the only live tracker for its device (registering on construction, unregistering on destruction, keyed by the device handle). The assertion is what keeps "standalone-constructible" from becoming "accidentally duplicated"; without it, the freedom to construct one outside `DeviceContext` would be a silent trap.

**Alternatives rejected:** putting the tracker inside `AllocatorState` (it is a completion ledger, not an allocation concern, and it would force every allocator user to reason about epochs); making a `DeviceContext` mandatory for standalone tests (it would invalidate a pinned capability to satisfy a new one, and it would make `DeviceContext` a mandatory gateway for every device-scoped facility — a larger architectural statement than this change should make).

### D7: `BeginEpoch` goes at the end of a successful `StartFrame`

Not at `BeginMainCommandBuffer`, for two reasons:

1. `PreGPUStep` (`MainClass.cpp:291`) runs **before** `BeginMainCommandBuffer` (`:293`) and already resizes buffers. An epoch opened at `BeginMainCommandBuffer` would leave that work outside any epoch.
2. `StartFrame` can return early when image acquisition fails (`MainClass.cpp:283-289` skips the frame). Opening an epoch before acquisition would create an epoch whose submission never happens — an epoch that can never complete, stranding everything parked under it.

Placement after acquisition and after the command buffer reset, immediately before returning success, satisfies both. A skipped frame opens no epoch, and `MainClass` returns before `PreGPUStep`, so no allocation happens outside an epoch either.

`AbortEpoch()` covers the remaining error paths (a submission that throws between `BeginEpoch` and `EndEpoch`), so a failure cannot strand an epoch either.

**Consequence recorded:** `PreGPUStep` currently allocates *before* `BeginEpoch` during the migration window of the later `physics-step-simplification` change. For this change it is inside the epoch, which is what we want; the ordering above is chosen so it stays correct when that call is deleted.

### D8: One watermark per frame covers both of the frame's submissions

The staged upload is submitted mid-epoch and the main batch waits on its timepoint 2; `command_executed_fences[fif]` is attached to the main submit. Therefore observing the frame fence proves the whole frame — upload, main batch, and copy batch — complete, and a single report suffices.

**Alternative rejected:** giving the staged upload its own epoch. It adds a watermark per frame and a second reporting point, with no gain in safety.

### D9: `SubmissionHelper` becomes the second driver, and its staging joins retirement

Once the sink is installed, staging buffers are `BufferAllocation`s too and would be parked by default. That would be a **memory regression** for uploads (staging holds whole textures). The fix is the protocol, not an exemption:

```
ExecuteSubmissionImmediately:  submit -> waitForFences -> ReportComplete(epoch) -> release staging
OnBatchComplete (deferred):    waitForFences -> ReportComplete(epoch) -> release staging
```

With the immediate-release rule of D4, staging is then released in the same cycle it always was, while the parking path still protects anything that was not proven complete. This also makes the class of bug fixed by the 2026-08-07 staging change structurally unreachable: each batch's resources are parked under their own watermark.

**Note:** `SubmissionHelper`'s per-batch staging containers (`m_pending_staging` / `m_active_staging`) are *not* removed by this change — they still own the allocations until the release point. Consolidating them into the retirement queue is a follow-up (see Open Questions).

### D10: `model_matrices` becomes stably owned

Today the physics scene owns `model_matrices` through `unique_ptr` while the render side borrows it as a raw pointer in two places: `SceneDataManager` stores `const ComputeBuffer *`, and `RenderGraphBuilder::ImportExternalResource` stores `&buffer` (recorded in the archived `physics-app-modes` design as a known dangling hazard). `PhysicsScene::RefreshGpuBuffers` replaces that buffer whenever the rigid-body slot count changes.

Because the graph is built once and recorded every frame, a body-count change after graph construction leaves the graph holding a destroyed buffer. Retirement alone does **not** fix this: parking the allocation keeps the memory alive but the `ComputeBuffer` facade is gone, and the render side's pointer is to the facade. So this change adds a shared-ownership factory and makes the forwarding hand over a stable reference, which additionally means the retired allocation is held by a live reference rather than only by the retirement queue.

### D11: No in-flight depth in the public interface

An earlier draft exposed the in-flight depth so host-visible parameter buffers could rotate per epoch. The later decision to move CPU-known kernel parameters into push constants removes that consumer; nothing else needs the number (the descriptor arena of the follow-up change needs epoch *completion*, not a depth). The interface therefore omits it, and `slot_count`-style hand-passed depths are not replaced.

## Risks / Trade-offs

- **[Retirement masks use-after-free instead of exposing it]** → Mitigation: parking is bounded and observable; a debug counter of parked items and outstanding watermarks is part of the implementation, and the headless fixture asserts parked items drain to zero after a device-idle wait.
- **[An epoch that never completes strands resources]** → Mitigation: `AbortEpoch` for error paths; no epoch is opened for a skipped frame (D7); the tracker's destructor releases everything; a debug assertion bounds the number of outstanding watermarks.
- **[Completion reported too early reintroduces the original bug]** → Mitigation: the contiguous-prefix rule (D3) plus a single reporting point per submitter; reporting happens only after a `waitForFences` return, never on a timer or a poll.
- **[Staging regression if a submitter forgets to report]** → Mitigation: the two submitters are the only producers (D8, D9), the immediate-release rule of D4 makes the common case immediate, and the spec requires reporting before releasing staging.
- **[Recorded-but-unsubmitted work retaining buffers for a whole epoch]** → Accepted, not mitigated: an allocation bound into a command buffer that is still being recorded must stay alive until that epoch completes (D4). Hot loops are expected to reuse instance-held scratch; a per-call temporary pays this cost by design. A debug counter makes the retained set visible.
- **[Destroy-order hazards at teardown]** → Mitigation: the tracker is declared so it is destroyed before the allocator; a buffer outliving the `DeviceContext` is already undefined today and is not made worse.
- **[Two id spaces (watermarks vs timeline values) diverge]** → Mitigation: watermarks are never given to Vulkan and timeline values are never used for retirement; the only link is the report call.

## Migration Plan

1. Introduce the tracker and the sink; wire `DeviceContext`, `AllocatorState`, `DeviceContext` teardown. No behavior change yet, since nothing reports completion — the facility starts fully conservative.
2. Make `SubmissionHelper` a driver (D9) so blocking paths reclaim immediately.
3. Make `FrameManager` a driver (D7, D8): open at the end of `StartFrame`, report after the fence wait; drain at device-idle points.
4. Convert `model_matrices` to shared ownership (D10) and update the four call sites.
5. Add the headless fixture that changes geometry between steps and asserts the parked set drains.

Rollback is per-step: with all reporting removed, the facility degrades to "retain until device idle", which is safe but leaky; each step is independently revertible.

## Related changes

This is the first of four planned changes; the others build on it and are planned separately.

- `descriptor-arena-epoch-buckets` — descriptor-set lifetime and pool management, built on the epochs introduced here. Its reuse cache is only sound because epoch completion is observable, and it is the reason this change's protocol deliberately does **not** need a per-submission event (see D4).
- `compute-kernel-dispatch` — the name→resource dictionary dispatch surface, which obtains its sets from the arena above and re-acquires them on every dispatch.
- `physics-step-simplification` — deletes `PreGPUStep`/`PostGPUStep` and moves buffer sizing to record time, which this change is what makes safe.

## Open Questions

- **Aggressive reclamation.** Completing an epoch as soon as its timeline value is reached (one frame of retention instead of up to three) needs a non-blocking counter query on the previous frame's semaphore. Deferred: it changes only latency, not correctness, so it can be added without touching the specs.
- **Consolidating `SubmissionHelper` staging accounting into the retirement queue**, which would delete `m_pending_staging` / `m_active_staging` and their protocol. Deferred to keep this change small and because `submission-helper-sync` is covered by focused tests.
- **Extracting `FrameManager`'s timeline bookkeeping into RHI as a submission-timeline facility**, which would unify the two id spaces (D1) and make aggressive reclamation natural. Deferred because it touches a pinned spec and buys simplification rather than new safety.
