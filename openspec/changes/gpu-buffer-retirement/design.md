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
  :252-258  FlushPhysics -> SyncGpuBuffers -> ExecuteSubmissionImmediately   [epoch: flush]
                  its own fence wait reports it promptly, before StartFrame below
  :283      StartFrame          waitForFences(command_executed_fences[fif])  <- frame N-3 done
                  opens the frame's epoch
  :291      PreGPUStep                                                       <- may resize buffers
  :293      BeginMainCommandBuffer
  :294-297  GPUStep(cb) + RecordAllPasses(cb)
  :301      CompleteFrame -> SubmitFrame
                ExecuteSubmission(timepoint 2)   <- staged upload submit     [epoch: upload]
                                                      its fence is waited by OnBatchComplete,
                                                      which reports it promptly
                submit2(main CB + copy CB), signal timepoint 4 @ eAllCommands
                                            , wait prev frame timepoint 4 @ eAllCommands
                                                                             [epoch: frame]
  :307      PostGPUStep
```

`FrameManager::SubmitFrame` issues two submissions per frame and attaches `command_executed_fences[fif]` to the main one. The main batch waits on the staged upload's timepoint 2, so observing the frame fence proves both submissions complete — but the two still carry **separate epochs** (D8), because the upload's own fence is observable much earlier and is what lets its staging be reclaimed precisely (D9).

### Constraint from existing specs

`rhi-compute-resource-binding` forbids presentation vocabulary in RHI's public API. The facility introduced here is therefore expressed in terms of *epochs and watermarks*, never frames.

### Inter-frame serialization is an existing invariant

`FrameManager::SubmitFrame` makes each frame's batch wait on the previous frame's timeline at its expected timepoints with `eAllCommands` (`FrameManager.cpp:281-284`), so **frame N's commands cannot begin before frame N-1's commands have finished**. Two consequences matter here:

- Physics' cross-frame dependency (frame N reads and writes the same buffers frame N-1 did) is already satisfied by this wait, so this design adds nothing for it.
- That dependency is satisfied *incidentally*, by the render side, and the same wait is what serialises the whole GPU pipeline. It is therefore a natural future optimisation target, and removing it would silently break physics — no error, just wrong simulation results.

The invariant is recorded here as a precondition this design relies on and does not change: **inter-frame GPU serialization is provided by the frame submission's previous-frame wait and MUST NOT be removed silently.** Work that genuinely needs to run independently of the frame must go to a separate queue with its own epochs and its own completion reporting — the submitter protocol in this change is submitter-agnostic and supports that — rather than by relaxing the wait.

### Prefix advancement depends on every submitter reporting

The completed prefix advances only when submitters report, and nothing in the engine makes that happen automatically. Every epoch must therefore reach exactly one of two terminal states — reported (with or without a completion signal) or abandoned — and a submitter that stops reporting strands every resource parked under its epochs *and* every resource parked under any later epoch, because the prefix cannot jump over the gap.

This is stated as an explicit dependency rather than left implicit for two reasons:

- The render side's frame-to-frame serialisation (above) bounds how long a lag lasts; it is **not** what makes the mechanism correct. Reading it as the safety argument would mean the mechanism silently depends on a render-side detail.
- The observation points are coarse and late by construction: a frame's completion is noticed only at `StartFrame`, three frames after it was submitted. The prefix therefore lags by about one in-flight depth even when the GPU is idle, and that lag is the price of the upper-bound parking mode (D4). The exact mode exists to avoid paying it where the referencing submission is known.



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

### D2: A report proves one epoch; the prefix is derived from many

The tracker keeps two separate things, and the distinction is load-bearing:

```
  reported          : the set of watermarks individually reported  (sparse, out of order)
  completed_prefix  : the largest n such that every watermark <= n is reported
```

A report is proof of exactly the epoch it names. The tracker cannot observe a `vkQueueSubmit2`, so "the submission carrying epoch w has finished" is the *only* fact a report carries; it is not evidence about any other epoch. The prefix is a *derived* statement about a contiguous range, and it advances only through the contiguous run of reports.

Both of these hold at the same time, and conflating them is the mistake this decision exists to prevent:

- an epoch can be reported while the prefix is far below it;
- the prefix can be far below an epoch whose GPU work finished long ago.

### D3: The prefix lags by design, and it is a correctness requirement

An earlier draft of this document claimed that because the GPU is serialised across frames and all submissions go to one queue, completions are "effectively in order" and the contiguous-prefix rule is "insurance, not a throughput cost". **That is false, and the corrected picture is what the rest of the design depends on.**

CPU-side reports are routinely out of order:

```
  frame N-3's epoch is reported only when StartFrame(N) waits the fif fence   (FrameManager.cpp:211-215)
  while the blocking flush at MainClass.cpp:258 reports its own, newer epoch *before* that
  => the prefix is pinned by the oldest unreported epoch, sitting near w_{N-3}
```

So the prefix lags by roughly the in-flight depth, and the rule is a **correctness requirement**, not a free safety net. Its cost is *retention time*: everything parked in the upper-bound mode is held until the prefix catches up.

**Why the lag is inherent to the observation points.** The CPU learns that a frame completed only where it waits a fence, and `StartFrame` waits the fence of the frame that used the same in-flight slot — three frames earlier. Nothing else observes a frame's completion, and reporting earlier would assert a completion that has not been observed. The lag is therefore bounded by the in-flight depth, and that bound holds only because submitters eventually report every epoch they open — recorded as an explicit dependency in the Context above rather than left implicit.

**Considered and rejected: polling the timeline to report earlier.** `FrameManager` already signals a timeline value at `eAllCommands`, so a non-blocking counter query per in-flight frame would let the CPU report as soon as the GPU is actually done, shrinking the lag towards the GPU's true progress. It is rejected because it introduces a driver query into the meaning of an epoch: the facility would then be reasoning about a semaphore's counter rather than about reports it was given. The lag is accepted instead, and the exact parking mode of D4 is what avoids paying it where the referencing submission is known.

### D4: Two parking modes, because a report proves only one epoch

```
  Upper-bound mode:  the referencing epochs are a subset of {<= bound}
                     release when  completed_prefix >= bound
                     default for ~BufferAllocation; bound = newest watermark at release time

  Exact mode:        the referencing epoch is exactly {e}          (the caller's claim)
                     release when  e is reported
                     used for staging, whose referencing submission the caller just waited on

  Both conditions are evaluated when a resource is parked and again after every report.
  A condition that already holds at park time releases immediately — there is no separate
  "immediate release" rule.
```

**Why two modes rather than one.** They answer the two halves of the same ignorance. We cannot know "everything at or below e has completed", so either we take an upper bound and wait for the prefix (safe, and it pays the lag of D3), or the caller narrows the referencing set to a single epoch (safe, and it pays nothing, because a report for that epoch is exactly the proof required).

**Why the upper-bound mode is sound.** A resource released at time T cannot be bound by an epoch whose recording begins after T, because binding a released resource is not a legitimate operation. The referencing epochs are therefore a subset of {≤ bound}. That argument does not depend on epochs being non-nested, nor on the CPU processing epochs serially — overlapping epochs are precisely why the prefix is also required.

**Why the exact mode is sound, and why misusing it is safe.** The caller's claim narrows the referencing set; the release still waits for a *report* for the named epoch, which the tracker can verify. A caller naming an unreported epoch gets retention, not a premature free. The escape hatch therefore cannot become a use-after-free — it can only cost time.

**Why the creation-time rule was removed.** An earlier draft released an allocation immediately when it had been created during the current epoch and no submission had yet been issued for that epoch. That rule is **unsound**, because recording is not submission:

```
  allocate A -> bind A into a dispatch recorded into the frame's command buffer -> release A
  A is referenced by a command buffer that is recorded but NOT yet submitted
  releasing A frees the VkBuffer; the eventual submission then references destroyed memory
```

"Not yet submitted" was mistaken for "not yet referenced". The rule's motivation was real — per-call temporaries must not accumulate for a whole epoch — but the answer is the usage rule rather than early release: hot loops reuse a buffer they already own, and a per-call temporary legitimately stays alive until the prefix reaches its bound. That usage rule is already the chosen pattern (instance-held scratch with grow-only capacity).

**The submission notification is removed with it.** That protocol step existed only so the facility could answer "has anything been submitted in this epoch", which only the removed rule needed.

**A tightening that is deliberately not taken.** Tracking the epoch in which each allocation was *last bound* would tighten the upper bound. It is not needed for correctness, it would require a stamp on every binding path, and it takes the release decision away from the caller. The retention it would save is bounded by the in-flight depth, and the case only arises when a buffer is replaced — a geometry change, where the memory is being replaced anyway.

### D5: The cut goes in `BufferAllocation`, not in `ComputeBuffer`

| Option | Change surface | Verdict |
|---|---|---|
| **A. `BufferAllocation` holds an optional hand-off to the tracker** | 92 buffer declarations, 5 `EnsureBuffer` copies, and every algorithm contract change **not at all**; every existing buffer becomes retire-safe at once | **Chosen** |
| B. New subclass (e.g. a managed compute buffer) | ~92 declarations, 5 `EnsureBuffer` copies, `RadixSortBuffers`-style contracts, and coexistence with the existing `unique_ptr<ComputeBuffer>` signatures | Rejected: same guarantee as A for a much larger diff |
| C. Outer wrapper handle | Same diff as B plus conversions back to `DeviceBuffer &` for every binding call | Rejected |

`BufferAllocation` is the object that actually holds the `VkBuffer` and VMA allocation, so it is the last place that can decide whether freeing now is safe. `ComputeBuffer` / `DeviceBuffer` / `IndexedBuffer` are facades over it and need no change. The hand-off target is the tracker itself: `BufferAllocation` and `AllocatorState` hold an `EpochTracker *`, forward-declared, so `MemoryAllocation.h` and `AllocatorState.h` still do not include it.

**Why there is no retire-sink interface (a reversal).** An earlier draft put an `IBufferRetireSink` interface in `Device/` and had `EpochTracker` implement it. Its stated justification was dependency direction — the sink interface lives on the `Buffer` side and the tracker implements it, so `Buffer/` does not depend on `Submission/` — and that justification does not survive being checked:

- **Nothing else implements it.** The tracker is the only implementer, no planned change adds another (the follow-up changes read the completed *prefix*; they do not install a retirement policy), and the one other implementer was a test double. The interface therefore advertised a substitutability that does not exist, and a reader could reasonably conclude that some other allocation manager is expected to appear.
- **It bought no dependency isolation.** `DeviceContext` already includes `Submission/EpochTracker.h` and holds the tracker by `unique_ptr`, so the `Device/` → `Submission/` edge exists with or without the interface. A concrete pointer needs only a forward declaration in the two headers that name it today — exactly what the interface needed.

So the tracker is named directly, and the requirement that it is the *sole* recipient of retired allocations is stated in `rhi-module` rather than left as an implied property of a one-implementation interface.

### D6: The tracker is standalone-constructible; `DeviceContext` owns one and wires the hand-off

Two setups exist in the repo and both must keep working:

- **`DeviceContext`** (what `RenderSystem` and the physics system use) owns the tracker, installs it on the allocator as the hand-off target, and is the single place that guarantees the tracker outlives the allocator and the device.
- **Standalone headless setups.** `test/unit/rhi/rhi_standalone_test.cpp` and `test/unit/rhi/submission_helper_test.cpp` construct a `DeviceInterface` and an `AllocatorState` directly with no `DeviceContext`, and `rhi-module` pins that this is supported. The tracker SHALL therefore be constructible from those same facilities and installable through the same setter, so a standalone program can opt into retirement. Without a tracker installed, no hand-off target is set and allocations destroy immediately — exactly today's semantics, which is the correct fallback for a setup that never defers work.

`AllocatorState` gains a **setter** rather than a constructor parameter, because `AllocatorState(DeviceInterface &)` is pinned by `rhi-module` and `gpu-context-module`; a setter leaves both intact.

`SubmissionHelper` takes the tracker as an explicit construction dependency (see the spec delta) rather than reaching it through `AllocatorState`, so that a standalone setup that never installs a tracker still cannot construct a submitter that reports into nothing.

**A device has exactly one tracker.** Two trackers on one device are a programming error, not a safety hole: watermarks are self-issued, so a mismatched pair would park resources under watermarks that never complete — a leak, never a dangling reference. It is still worth catching, so the tracker SHALL assert in debug builds that it is the only live tracker for its device (registering on construction, unregistering on destruction, keyed by the device handle). The assertion is what keeps "standalone-constructible" from becoming "accidentally duplicated"; without it, the freedom to construct one outside `DeviceContext` would be a silent trap.

**Alternatives rejected:** putting the tracker inside `AllocatorState` (it is a completion ledger, not an allocation concern, and it would force every allocator user to reason about epochs); making a `DeviceContext` mandatory for standalone tests (it would invalidate a pinned capability to satisfy a new one, and it would make `DeviceContext` a mandatory gateway for every device-scoped facility — a larger architectural statement than this change should make).

### D7: `BeginEpoch` goes at the end of a successful `StartFrame`

Not at `BeginMainCommandBuffer`, for two reasons:

1. `PreGPUStep` (`MainClass.cpp:291`) runs **before** `BeginMainCommandBuffer` (`:293`) and already resizes buffers. An epoch opened at `BeginMainCommandBuffer` would leave that work outside any epoch.
2. `StartFrame` can return early when image acquisition fails (`MainClass.cpp:283-289` skips the frame). Opening an epoch before acquisition would create an epoch whose submission never happens — an epoch that can never complete, stranding everything parked under it.

Placement after acquisition and after the command buffer reset, immediately before returning success, satisfies both. A skipped frame opens no epoch, and `MainClass` returns before `PreGPUStep`, so no allocation happens outside an epoch either.

`AbandonEpoch()` covers the remaining error path: a submission that was never issued (it threw, or the work was dropped) leaves an epoch that would otherwise never be reported. It marks the epoch as reported **without** a completion signal, so the prefix can advance past it; it does **not** release anything directly, because a resource parked under the abandoned epoch may still be referenced by an earlier, unreported epoch. Abandonment is only valid when no submission was issued for that epoch — if one was issued and its completion cannot be observed, that failure must be surfaced rather than masked.

**Consequence recorded:** `PreGPUStep` currently allocates *before* `BeginEpoch` during the migration window of the later `physics-step-simplification` change. For this change it is inside the epoch, which is what we want; the ordering above is chosen so it stays correct when that call is deleted.

### D8: Each submission gets its own epoch, including the frame's staged upload

An earlier draft gave the frame's staged-upload submission and the frame's main batch **one** watermark, on the grounds that the main batch waits on the upload's timepoint 2 and the fence is attached to the main submit — so one report covers both, and a second watermark looked like pure overhead. **That is reversed here**, because it makes the upload's staging impossible to reclaim precisely:

```
  with one epoch per frame:
      the staged upload is submitted *inside* the frame's epoch w_N
      => the staging's referencing epoch IS w_N
      => the exact mode (D4) would wait for w_N, which is reported three frames later
      => staging is retained for a full in-flight depth, which is exactly the regression
         D9 exists to avoid

  with one epoch per submission:
      the upload gets w_up, opened by the submitter that issues it
      its report point is that submitter's own fence wait — precise and immediate
      => the staging's exact-mode release fires as soon as the fence is waited
```

The frame's epoch still covers the main batch (and the copy batch, which shares the main submit's fence), so nothing about frame-level reclamation changes. The cost is one extra watermark and one extra report per frame, which is what buys staging back its today-behaviour memory profile.

### D9: `SubmissionHelper` becomes the second driver, and its staging is reclaimed by the exact mode

Once the tracker is installed, staging buffers are `BufferAllocation`s too and would be parked by default. That would be a **memory regression** for uploads (staging holds whole textures). The fix is a mechanism, not an exemption:

```
  ExecuteSubmissionImmediately:  open w -> submit -> waitForFences -> report w -> release staging
  OnBatchComplete (deferred):    waitForFences -> report w_up -> release staging
                                 (w_up = the epoch this helper opened for the upload submission)
```

Staging is released under the **exact mode** with the epoch of the submission that carried it (D4), so the release condition is already satisfied at the moment of release and the staging is freed in the same cycle it always was.

Two rules make this correct, and both are new here:

- **Who opens reports.** A submitter may report only epochs it opened itself. In particular `SubmissionHelper` must **not** report the frame's epoch on `OnBatchComplete`: the upload's fence says nothing about the frame's main batch, which is still in flight, and reporting the frame's epoch there would be a false assertion that frees everything parked under it.
- **The upload's epoch is the helper's own** (D8), which is what gives the staging a precise epoch to name.

This also makes the class of bug fixed by the 2026-08-07 staging change structurally unreachable: each batch's resources are parked under an epoch only that batch's submitter can report.

**Note:** `SubmissionHelper`'s per-batch staging containers (`m_pending_staging` / `m_active_staging`) are *not* removed by this change — they still own the allocations until the release point. Consolidating them into the retirement queue is a follow-up (see Open Questions).

### D10: `model_matrices` becomes stably owned

Today the physics scene owns `model_matrices` through `unique_ptr` while the render side borrows it as a raw pointer in two places: `SceneDataManager` stores `const ComputeBuffer *`, and `RenderGraphBuilder::ImportExternalResource` stores `&buffer` (recorded in the archived `physics-app-modes` design as a known dangling hazard). `PhysicsScene::RefreshGpuBuffers` replaces that buffer whenever the rigid-body slot count changes.

Because the graph is built once and recorded every frame, a body-count change after graph construction leaves the graph holding a destroyed buffer. Retirement alone does **not** fix this: parking the allocation keeps the memory alive but the `ComputeBuffer` facade is gone, and the render side's pointer is to the facade. So this change adds a shared-ownership factory and makes the forwarding hand over a stable reference, which additionally means the retired allocation is held by a live reference rather than only by the retirement queue.

### D11: No in-flight depth in the public interface

An earlier draft exposed the in-flight depth so host-visible parameter buffers could rotate per epoch. The later decision to move CPU-known kernel parameters into push constants removes that consumer; nothing else needs the number (the descriptor arena of the follow-up change needs the completed *prefix*, which it reads from the tracker rather than being told a depth). The interface therefore omits it, and `slot_count`-style hand-passed depths are not replaced.

## Risks / Trade-offs

- **[Retirement masks use-after-free instead of exposing it]** → Mitigation: parking is bounded and observable; a debug counter of parked items and outstanding watermarks is part of the implementation, and the headless fixture asserts parked items drain to zero after a device-idle wait.
- **[An epoch that never reaches a terminal state strands everything above it]** → Mitigation: `AbandonEpoch` for epochs that produced no work; no epoch is opened for a skipped frame (D7); the tracker's destructor releases everything; a debug assertion bounds the number of outstanding watermarks and the condition is part of the spec.
- **[Abandonment used as a way to force a release]** → Mitigation: the spec states that abandonment marks the epoch as reported and MUST NOT release anything directly; a test asserts that resources parked under an abandoned epoch survive while earlier epochs are unreported.
- **[A submitter reports an epoch it did not open]** → Mitigation: stated as a protocol rule with its own scenario; the practical case (`SubmissionHelper` reporting the frame's epoch on `OnBatchComplete`) is called out explicitly in D9.
- **[Completion reported too early reintroduces the original bug]** → Mitigation: reports happen only after a `waitForFences` return, never on a timer or a poll; the prefix rule (D3) then ensures a single out-of-order report releases nothing.
- **[Staging regression if the upload's epoch is not precise]** → Mitigation: the upload gets its own epoch (D8) and the staging is parked in the exact mode (D9), so its release fires at the fence wait; a test asserts staging is not retained for a full in-flight depth.
- **[The prefix lags further than the in-flight depth]** → Accepted and bounded: the lag is set by the coarsest reporting point (`StartFrame`'s fence wait for a three-frames-old submission), and the exact mode is available where the referencing submission is known. Polling the timeline to shrink it was considered and rejected (D3).
- **[Recorded-but-unsubmitted work retaining buffers for a whole epoch]** → Accepted, not mitigated: an allocation bound into a command buffer that is still being recorded must stay alive until that epoch completes (D4). Hot loops are expected to reuse instance-held scratch; a per-call temporary pays this cost by design. A debug counter makes the retained set visible.
- **[Destroy-order hazards at teardown]** → Mitigation: the tracker is declared so it is destroyed before the allocator; a buffer outliving the `DeviceContext` is already undefined today and is not made worse.
- **[Two id spaces (watermarks vs timeline values) diverge]** → Mitigation: watermarks are never given to Vulkan and timeline values are never used for retirement; the only link is the report call.

## Migration Plan

1. Introduce the tracker and the allocator's hand-off to it; wire `DeviceContext` and the tracker's teardown. No behavior change yet, since nothing reports — the facility starts fully conservative.
2. Implement both parking modes (D4) and the reported-set / prefix model (D2), with the debug counters.
3. Make `SubmissionHelper` a driver (D9): it opens its own epoch per submission, reports only what it opened, and releases staging under the exact mode. This is what restores today's staging memory profile.
4. Make `FrameManager` a driver (D7, D8): open the frame's epoch at the end of `StartFrame`, report it after the slot's fence wait, and release parked resources at device-idle points.
5. Convert `model_matrices` to shared ownership (D10) and update the four call sites.
6. Add the headless fixture that changes geometry between steps and asserts the parked set drains.

Rollback is per-step: with all reporting removed, the facility degrades to "retain until device idle", which is safe but leaky; each step is independently revertible.

## Related changes

This is the first of four planned changes; the others build on it and are planned separately.

- `descriptor-arena-epoch-buckets` — descriptor-set lifetime and pool management, built on the epochs introduced here. It reads the completed **prefix** (not individual reports) to decide what may be reclaimed, and its reuse cache is only sound because that prefix is observable. It is also the reason this change's protocol deliberately does **not** need a per-submission event (see D4).
- `compute-kernel-dispatch` — the name→resource dictionary dispatch surface, which obtains its sets from the arena above and re-acquires them on every dispatch.
- `physics-step-simplification` — deletes `PreGPUStep`/`PostGPUStep` and moves buffer sizing to record time, which this change is what makes safe.

## Open Questions

- **Consolidating `SubmissionHelper` staging accounting into the retirement queue**, which would delete `m_pending_staging` / `m_active_staging` and their protocol. Deferred to keep this change small and because `submission-helper-sync` is covered by focused tests.
- **Extracting `FrameManager`'s timeline bookkeeping into RHI as a submission-timeline facility**, which would unify the two id spaces (D1). Deferred because it touches a pinned spec and buys simplification rather than new safety.

**Considered and rejected: polling a timeline counter to report earlier.** `FrameManager` already signals a timeline value at `eAllCommands` per frame, and a non-blocking `vkGetSemaphoreCounterValue` would let the CPU report a frame as soon as the GPU is actually done — shrinking the prefix's lag from "one in-flight depth" to "the GPU's true progress". It is rejected (D3): it puts a driver query into the meaning of an epoch, so the facility would be reasoning about a semaphore counter instead of about reports it was given. The lag is accepted, and the exact mode is the answer where the referencing submission is known.
