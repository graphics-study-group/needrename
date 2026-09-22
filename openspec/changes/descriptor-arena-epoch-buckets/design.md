# Design — descriptor-arena-epoch-buckets

See `proposal.md` — Why for the motivation. This document records the decisions that shape the arena, the traps it must avoid, and what is deliberately left out.

## Context

### Current state

Three facts from the existing code determine the whole design:

1. **Two content-hash caches, both unbounded.** `ShaderResourceBinding::impl::descriptor_sets` is `std::unordered_map<size_t, std::unordered_map<size_t, vk::DescriptorSet>>` keyed by a layout hash and a content hash of the bound `(name, handle, offset, size)` triples; a miss calls `allocateDescriptorSets` (`ShaderResourceBinding.cpp:190-192`) and stores the result, and nothing is ever erased. `MaterialInstance::desc_set_cache[backbuffer]` keeps only the newest set per (pass, slot), so the rest of what the cache mints is unreachable but still allocated.
2. **Four pool owners, all with a fixed budget that never resets.** `ComputeStage` creates one pool per stage with `MAX_COMPUTE_DESCRIPTORS_PER_POOL = 128` (`ComputeStage.cpp:20`, `:65-68`); `MaterialLibrary` creates one per material template (`MaterialLibrary.cpp:126-132`); `SceneDataManager` (`:140`) and `CameraManager` (`:88`) create their own. None is reset, and no allocation site handles exhaustion.
3. **Reallocation changes the content hash.** Every `EnsureBuffer` that changes a buffer handle changes the hash, so it mints a set that is never released. With roughly forty compute stages in physics, the compute path is the one that exhausts its budget first.

### Two release triggers, because two churn profiles

This is the fact the whole design turns on:

| | compute path (`ComputeStage` / `ComputeResourceBinding`) | material, scene and camera state (`ShaderResourceBinding`, `SceneDataManager`, `CameraManager`) |
|---|---|---|
| how often bound content changes | often — a buffer handle changes on every reallocation | rarely — rebinding textures or buffers; the per-in-flight-slot uniform-buffer difference is a **dynamic offset** and does not enter the hash |
| how a set stops being needed | the binding content simply stops being requested; nobody is left to tell the arena | the owner knows: the material instance, scene or camera is destroyed, or its own cache evicts |
| therefore the release trigger | **cache pressure** — discard the least recently acquired entries that are provably unreferenced | **the owner** — an explicit release, deferred if the entry is still referenced |

A single trigger is wrong either way: pressure-driven only cannot honour an owner that keeps a handle across frames (`MaterialInstance::desc_set_cache[backbuffer]` does exactly that), and owner-driven only leaves the compute path with nobody to release anything.

### Constraint from existing specs

`rhi-compute-resource-binding` forbids presentation vocabulary in RHI's public API, so the arena is expressed in terms of epochs, never frames.

### What the current leak is load-bearing for

Because sets are never freed, an in-flight command buffer never observes a freed or rewritten set. So the requirement is not "stop leaking" but "**reclaim only what is provably unreferenced**": a set may be released only once every epoch that could have bound it has completed, and it must never be rewritten in place. Both are possible only because `gpu-buffer-retirement` introduced epochs and completion reporting. This change depends on that facility; it does not restate its contract.

### Constraint inherited from the change that precedes this one

`rhi-module` is modified by `gpu-buffer-retirement` as well. The `## MODIFIED Requirements` block for that capability here is based on *that change's delta text*, not on the current `openspec/specs/` text, because `gpu-buffer-retirement` is archived first and its delta is what will be in the spec by then. Capabilities that change A does not touch (`rhi-compute-resource-binding`, `rhi-directory-structure`) are based on the current spec text.

### The two driver shapes the arena must serve

```
  FrameManager-driven (epoch opened before recording)      record-then-submit (tests, PhysicsApp, SubmissionHelper)
  ---------------------------------------------------      ------------------------------------------------------
  StartFrame -> BeginEpoch(w)                             record CB1  -> acquire sets   (no epoch open: pinned)
  record CB   -> acquire sets (stamped w)                 BeginEpoch(w1) -> claims them
  SubmitFrame -> submit(w)                                submit(w1)
  ...                                                     record CB2  -> acquire sets   (stamped w1)
  StartFrame(frame+3) -> Complete(w)                      BeginEpoch(w2) -> claims them
                                                          Complete(w1) -> w1's entries become evictable
```

Both drivers exist today. They differ in one way that matters: the first keeps several epochs outstanding at once, so a set acquired in one epoch is routinely re-acquired before that epoch completes; the second completes each epoch inside the same step, so nothing is ever re-acquired *before* completion. The design must not assume either.

## Goals / Non-Goals

**Goals**

- Give descriptor sets a bounded lifetime on every path, with no consumer-owned pool and no fixed per-consumer set budget.
- Make reuse the normal case: in steady state the arena should mint nothing, because the binding content of a kernel or a material is stable from frame to frame.
- Make reclamation a consequence of epoch completion, so nothing depends on polling, frame counters, or a caller remembering to clean up.
- Keep the change invisible to physics: no physics file, shader, or binding call site changes here.

**Non-Goals**

- No descriptor-set churn reduction for the material path. Resolving interface names into a dense table once, so that per-frame hashing stops touching strings, is a separate follow-up.
- No automatic barriers; barrier placement stays manual and out of this change.
- `slot_count` uniform-buffer rotation still exists after this change. It is removed only by the later `compute-kernel-dispatch` and `physics-step-simplification` changes.
- No change to what a descriptor set contains, to shader interfaces, or to the reflection path.

## Decisions

### D1: One arena, two release triggers

The arena owns every pool and shares pool growth, descriptor-set-layout reuse through `ImmutableResourceCache`, and debug naming across both triggers. It does not share a *release trigger*, because the two consumer families need opposite ones (see Context).

**Alternatives rejected:**

- *Pressure-driven only.* Cannot honour an owner that holds a set handle across frames; the handle would be released underneath it.
- *Owner-driven only.* The compute path has no owner per set — `ComputeKernel::Dispatch` mints a set per binding state and keeps no record of it — so nothing would ever be released.
- *Two separate classes.* They would duplicate pool growth, layout lookup and naming, and would leave two places that decide how a pool is grown.

### D2: One flat content-keyed cache; entries record the epoch in which they were last acquired

```
  Key   = (descriptor-set layout, dynamic-offset flags, resolved binding entries)
  Entry = { vk::DescriptorSet set;  last_epoch;  the pool that served it;  acquisition order }
```

A hit updates `last_epoch` to the current epoch and returns the same set. **Reuse across epochs is the point**, not an accident: a physics kernel's binding content is stable from frame to frame, so after the first frame the arena should mint nothing at all. (Buffer growth does not break this — it changes content only when a buffer is actually replaced, and change D makes replacement geometric and rare.)

What makes reuse safe is one invariant:

> a set is referenced only by command buffers of the epochs in which it was acquired

Together with the user obligation in D10, `last_epoch` is an upper bound on the epochs that can reference an entry, so an entry is releasable exactly when `last_epoch <= completed prefix`.

**Alternatives rejected:**

- *Intern inside a per-epoch bucket and never reuse across epochs* (an earlier draft of this design). Safe and simple, and it allows wholesale `vkResetDescriptorPool` release — but it re-mints every kernel's binding states every frame, forever, for content that is identical. The one thing it buys is a cheaper release path, and in steady state there is nothing to release.
- *A global cache with reference counting.* Reference counting re-derives what the epoch already knows, and one missed decrement reintroduces the leak.
- *One set per (binding, slot), rewritten in place when content changes.* An in-flight command buffer reads descriptors at execution time, not at record time, so a rewrite would be observed by the older frame — the hazard the current leak accidentally prevents.

### D3: The arena observes exactly one thing — the completed prefix advancing

The arena is not part of the submission protocol. It needs a single notification: the completed **prefix** advanced from `a` to `b`. It then considers releasing the entries whose `last_epoch` lies in `(a, b]`, and does so only if it is over budget (D5).

**It is the prefix, not individual reports.** A single report for epoch `w` proves only that `w`'s submission finished; it says nothing about earlier epochs, which may still be executing command buffers that bind the same set. An entry is therefore eligible only when the *prefix* has passed its `last_epoch`, never when its own epoch happens to be reported. Since the prefix advances only through a contiguous run of reports, and the preceding change documents that it lags by about the in-flight depth, an entry acquired recently stays ineligible for that long — which is correct, because those epochs really may still be binding it.

**Why the earlier four-event design is gone.** An earlier draft had the arena subscribe to begin / submit / complete / drain, with `submit` sealing a per-epoch bucket. That machinery existed to make *wholesale* per-epoch release safe: if a bucket could still receive sets after its epoch had been submitted, releasing the bucket would free sets a later, unsubmitted command buffer had bound. A resident cache that releases individual entries guarded by their own `last_epoch` has no bucket to seal, so the submit boundary is neither observable by nor useful to the arena. Fewer events also means fewer failure modes — there is no "forgot to seal" bug to have. (The preceding change reaches the same conclusion from its own side: its protocol no longer carries a per-submission event.)

**`drain` is not an arena event.** A device-idle wait proves that submitted work has finished; it does not make a live cache entry dead. Releasing the cache on idle would defeat it entirely on a fully serialised submitter — `PhysicsApp` waits for idle twice per step, so every entry would be discarded every step. Idle therefore reaches the arena only as "the prefix advanced to its maximum", which makes entries *eligible*; eviction still happens only under pressure (D5).

### D4: Entries acquired with no open epoch are pinned until an epoch claims them

The headless and physics-app paths record their own command buffer and only afterwards call a submission helper (`test/engine/headless/*`: `allocateCommandBuffers` + `begin`), so "acquire a set before any epoch exists" is the common case there, not an edge case.

Such an entry is marked **unclaimed** and is never evictable: the command buffer that bound it has not been submitted and will be submitted under whatever epoch comes next. When an epoch begins, it claims every unclaimed entry, stamping them with its watermark; from then on the ordinary rules apply. If no epoch ever opens, the entries stay pinned and are released when the arena is destroyed.

**`unclaimed` must be a distinct state, not `last_epoch = 0`.** Zero would compare as "already complete" and make the entry evictable immediately — the exact bug this rule exists to prevent.

### D5: Eviction is pressure-driven and epoch-guarded, with a soft budget

```
  when over budget:
      evict eligible entries, least-recently-acquired first
      eligible = claimed  &&  last_epoch <= completed prefix
```

Three properties are deliberate:

- **Pressure-driven, not age-driven.** An earlier draft released an entry as soon as the epoch in which it was acquired completed ("use it or lose it, per epoch"). That degenerates on a fully serialised submitter: `PhysicsApp` completes each step's epoch inside the step, so every entry was released every step and nothing was ever reused. Under a budget, entries persist while they keep being used and are discarded only when the cache needs the room.
- **Epoch-guarded.** The budget is a *desire*, not a licence: an entry still referenced by an outstanding epoch is never evicted. If every entry is ineligible, the arena exceeds the budget rather than corrupting an in-flight submission — so the budget is a soft cap, and the excess is bounded by the in-flight window.
- **Per-set release.** Returning sets individually requires `FREE_DESCRIPTOR_SET` on the pools and gives up the O(1) `vkResetDescriptorPool` that wholesale release allowed. That is the price of reuse, and it is small: in steady state almost nothing is released because almost nothing is minted.

**Alternative rejected:** an unguarded LRU (evict the least recently used entry when full). Its eviction decision is driven by capacity pressure rather than by "is this still referenced", so it can free a set an in-flight command buffer binds. Adding the guard is not an extra — the guard *is* the design, and with it the policy is simply "least recently acquired among the eligible".

### D6: The arena owns every pool; no consumer creates, owns, or resets one

Four sites create descriptor pools today: `ComputeStage` (one per stage), `MaterialLibrary` (one per material template), `SceneDataManager` and `CameraManager`. All four move onto the arena. Leaving any behind would make the capability's own requirement — that no consumer creates or owns a pool — false at archive time, and would leave that path's unbounded growth in place.

One concrete consequence to migrate: `MaterialLibrary` uses the pool handle as a sentinel for "this material has material descriptors" (`if (b.descriptor_pool)` at `MaterialLibrary.cpp:176`). That check must be replaced by a layout-derived condition (whether the reflected layout declares the material descriptor set), not by another pool.

`SceneDataManager`'s and `CameraManager`'s sets are long-lived, per-in-flight-slot state, so they take the owner-driven trigger (D1), not the pressure-driven one.

**Alternative rejected:** let each owner keep its pool and register it with the arena. That restores four pool owners, and sets could no longer be released uniformly — a set's pool would have to be supplied by the caller at release time, exactly the coupling being removed.

### D7: On-demand growth replaces the fixed set budget

A request that a pool cannot serve causes the arena to create another pool (and a failed allocation with an out-of-pool-memory result is retried once against a fresh pool). The arena reports no maximum number of sets to its callers. The resident-entry budget of D5 is a separate, soft bound on the cache, not a per-consumer set budget.

**Alternative rejected:** raising `MAX_COMPUTE_DESCRIPTORS_PER_POOL`. The value was never a guarantee, only a cliff; raising it moves the cliff.

### D8: `ShaderResourceBinding` keeps its role, loses its pool parameter, and gains a bound

The class still maps binding names to interfaces and still returns a descriptor set for a layout. What changes is where the set comes from and how long it lives: the pool parameter disappears, and its content cache becomes bounded — when it evicts an entry it *releases* the set, and the arena defers that release if the entry's epoch is still outstanding. The same guard as D5, applied to an owner-driven release.

Two hazards are carried forward unchanged, recorded so they are not mistaken for consequences of this change: the duplicate `// FIXME: Dynamic offset order might not be correct.` sites (`MaterialInstance.cpp:189`, `ComputeResourceBinding.cpp:132`), and the fact that the descriptor-set layout used for allocation and the one used for the pipeline layout are never asserted equal.

### D9: Declaration order in `DeviceContext`: the arena before the tracker

```
  declaration order:  DeviceInterface, AllocatorState, ImmutableResourceCache, DescriptorArena, EpochTracker
  destruction order:  EpochTracker -> DescriptorArena -> ... -> DeviceInterface
```

The tracker is destroyed before the allocator and device, as the preceding change requires. The arena is declared before it for a different reason than an earlier draft gave: the arena **registers with the tracker** as an observer of prefix advancement, so the tracker must not outlive its observer. (The earlier reason — the tracker holding parked release actions that call into the arena — no longer applies: buffer retirement and descriptor residency do not share a release path.)

**Alternative rejected:** make the arena's notification entry point tolerate being called after destruction. Silent no-ops would hide a real ordering bug.

### D10: The pressure-driven trigger carries a contract, and it is stated in the spec

The resident model is sound only under one obligation:

> A pressure-driven (compute-path) set must be re-acquired in **every** epoch whose command buffers use it, and its handle MUST NOT be cached across epochs.

Both halves matter. Without the first, `last_epoch` under-states the referencing epochs and an entry can be evicted while a later command buffer still binds it. Without the second, a caller holds a handle the arena may already have released.

The obligation is satisfied automatically on the only path that uses this trigger today: `ComputeKernel::Dispatch` acquires on every dispatch, so a kernel that dispatches in an epoch refreshes its entries and a kernel that does not simply stops refreshing them. Callers that *do* hold handles across frames — `MaterialInstance::desc_set_cache[backbuffer]` is the existing example — use the owner-driven trigger, which is what it exists for.

This is stated as a requirement in the capability spec rather than left implicit here, because it is the one thing a caller can get wrong.

## Risks / Trade-offs

- **[An entry is evicted while a recorded command buffer still binds it]** → Mitigation: eviction is guarded by `claimed && last_epoch <= completed prefix` (D5), and the re-acquire obligation that makes `last_epoch` an upper bound is a spec requirement (D10). Tests cover eviction pressure with entries still outstanding.
- **[A caller caches a pressure-driven set handle across epochs and uses a released set]** → Mitigation: the contract is stated in the spec, the only pressure-driven consumer acquires per dispatch, and the owner-driven trigger exists for callers that need to hold a handle.
- **[A fully serialised submitter re-mints everything]** → Mitigation: the budget, not the epoch, drives release (D5); the physics-app path is an explicit test fixture, since it is the shape that broke the earlier age-driven rule.
- **[Losing the per-consumer 128-set budget lets one consumer's burst starve others]** → Mitigation: the arena grows pools on demand rather than partitioning; the old budget was a cliff, not a guarantee; pool and set counts are observable for regression tracking.
- **[Cache growth if binding content churns every frame]** → Mitigation: the soft budget bounds the resident set, ineligible entries are the only ones allowed to exceed it, and a debug counter reports both the resident count and the number of entries held back by the guard.
- **[The completed prefix lags, so entries stay ineligible longer than they are actually in use]** → Accepted: the preceding change documents that the prefix lags by about one in-flight depth because a frame is reported only three frames after it was submitted. The consequence here is that the resident cache holds a window of roughly that many epochs' worth of set content, and that eviction under pressure may have nothing eligible to release. The budget is soft for exactly this reason; the counter of entries held back by the guard makes the condition visible.
- **[Teardown order inverted: the tracker notifying a destroyed arena]** → Mitigation: declaration order in `DeviceContext` (D9), plus a device-idle teardown test under the validation layer that leaves entries resident.
- **[Material path behaviour changes more than expected when its pool disappears]** → Mitigation: the sentinel replacement (D6) is identified up front; the material-churn test asserts sets are released on instance destruction and on cache eviction.
- **[Release becomes silent when a submitter stops reporting completions]** → Mitigation: entries simply stop becoming eligible, which the outstanding-watermark counter added by the preceding change already exposes; the arena's resident counter makes the growth visible.

## Migration Plan

1. Introduce the arena with its resident cache, the pinning rule (D4) and the guarded eviction (D5); wire `DeviceContext` with the declaration order from D9 and register it as an observer of the preceding change's watermark advancement. No behaviour change yet for any consumer.
2. Move the compute path onto the pressure-driven trigger: `ComputeStage` stops creating a pool and stops exposing one; `ComputeResourceBinding` acquires from the arena.
3. Move the material path onto the owner-driven trigger: `ShaderResourceBinding` loses its pool parameter and gains a bounded cache whose evictions release through the arena; update `MaterialInstance`; remove `MaterialTemplate`'s pool and replace the `MaterialLibrary` sentinel.
4. Move `SceneDataManager`'s and `CameraManager`'s pools onto the arena (D6), so no consumer owns a pool.
5. Add the verification fixtures: steady-state zero minting, reuse across epochs on both driver shapes, growth past the old budget, eviction under pressure with entries still outstanding, and release on instance destruction.
6. Run the full suite and validate.

Rollback is per-step. Steps 2, 3 and 4 are independent of one another once step 1 lands: any one can be reverted while the others stay, because the triggers do not share state beyond the arena's pool bookkeeping.

## Open Questions

- **The resident budget's default value.** The arena needs *a* number; it is a safety bound rather than a tuning knob, and normal operation should never reach it. The counters from step 5 are the input, so the value can be settled during implementation without changing the specs or the task breakdown.
- **Precomputing the material path's name-to-slot resolution.** It is the churn-reduction work this change explicitly excludes; it becomes natural once the arena owns acquisition, but it changes hashing behaviour and deserves its own change.
