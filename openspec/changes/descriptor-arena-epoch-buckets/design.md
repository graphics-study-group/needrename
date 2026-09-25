# Design — descriptor-arena-epoch-buckets

See `proposal.md` — Why for the motivation. This document records the decisions that shape the arena, the traps it must avoid, and what is deliberately left out.

> **On the name.** "Epoch buckets" is historical: the per-epoch bucket design was rejected while writing D2, and the two-release-trigger design that replaced it has since been rejected too (D6). The name is kept because the other changes in the sequence reference it by name.

## Context

### Current state

Four facts from the existing code determine the whole design:

1. **A content-hash cache that is never pruned, and a memo array that is not a cache.** `ShaderResourceBinding::impl::descriptor_sets` is `std::unordered_map<size_t, std::unordered_map<size_t, vk::DescriptorSet>>` keyed by a layout hash (of the `SPLayout` object's *address*, `:92`) and a content hash of the bound `(name, handle, offset, size)` triples; a miss calls `allocateDescriptorSets` (`ShaderResourceBinding.cpp:189-192`) and stores the result, and nothing is ever erased. Separately, `ComputeResourceBinding::impl::descriptor_sets[MAX_SLOT_COUNT]` (`ComputeResourceBinding.cpp:29`) and `MaterialInstance::impl::PassInfo::desc_set_cache[3]` (`MaterialInstance.cpp:39`) hold handles — but every read of them happens in the statement sequence that refreshed them (fact 4), so they are within-call memos, not cross-frame caches.
2. **Five pool owners, none of which resets.** `ComputeStage` creates one pool per stage with `MAX_COMPUTE_DESCRIPTORS_PER_POOL = 128` (`ComputeStage.cpp:20`, `:65-68`); `MaterialLibrary` creates one per material template (`MaterialLibrary.cpp:126-131`); `SceneDataManager` (`:251-256`) and `CameraManager` (`:57-60`) create their own; `GUISystem` (`:110-113`) creates one for the ImGui backend. The first four never reset and no allocation site handles exhaustion. Only the GUI pool sets `eFreeDescriptorSet`, because only ImGui frees its sets.
3. **Reallocation changes the content hash.** A buffer whose storage is replaced changes its handle, so the binding content changes and a set is minted that is never released. Since `stable-buffer-identity` a physics buffer's *object* survives a reallocation, but its storage — and therefore its handle — still changes whenever the requested size differs (`PhysicsScene.cpp:44`). Geometric growth is `physics-step-simplification`'s `rhi-buffer-capacity`, so it is not in force here: this change faces a path where a size change still changes the handle. With roughly forty compute stages in physics, the compute path is the one that exhausts its budget first.
4. **No consumer holds a set handle across epochs.** Every read of a stored handle immediately follows the call that refreshed it: `CommandBuffer::BindMaterial` (`:274-279`) and `SceneDataManager::DrawSkybox` (`:454-459`) call `UpdateGPUInfo` and then read and bind the handle on the following statements, and `Rhi::BindComputeResource` (`ComputeHelpers.cpp:16-19`) does the same. Nothing in the repository reads one of those handles in a later frame or a later epoch. That fact is what makes a single reclamation rule sufficient (D6) and a single contract statable (D10).

### What the current leak is load-bearing for

Because sets are never freed, an in-flight command buffer never observes a freed or rewritten set. So the requirement is not "stop leaking" but "**reclaim only what is provably unreferenced**": a set may be released only once every epoch that could have bound it has completed, and it must never be rewritten in place. Both are possible only because `gpu-buffer-retirement` introduced epochs and completion reporting. This change depends on that facility; it does not restate its contract.

### Two services, not two policies

The arena does two jobs, and keeping them apart is what makes the design coherent:

| | pool layer | cache layer |
|---|---|---|
| what it answers | "give me a set for this layout" | "give me the set for this content" |
| does the arena know the content | **no** — the caller writes the descriptors | yes — the arena resolves and writes them |
| is a set interned | no; a request always allocates | yes, by content |
| who decides the set's lifetime | nobody; it lives until the arena does | the arena, under pressure (D6) |
| who uses it | `SceneDataManager`, `CameraManager` | compute, material |

The pool layer is the primitive; the cache layer is built on it, since a cache miss allocates through it. Both share pool growth, the layout source (`ImmutableResourceCache`) and debug naming. D8 requires every pool to move here, so the pool layer is exactly what `SceneDataManager` and `CameraManager` need.

### Why `scene` and `camera` cannot use the cache layer

Two properties put them outside it:

- **Their sets are identified by in-flight slot, not by content.** `SceneDataManager` allocates `FRAMES_IN_FLIGHT` sets from one layout and distinguishes them only by the static offset of binding 0 (`SceneDataManager.cpp:211-218`); `CameraManager` does the same (`:112-126`). And binding 1 is an *array* of `MAX_SHADOW_CASTING_LIGHTS` combined image samplers, which the resolver does not support at all — `ShaderResourceBinding.cpp:125` asserts `array_size == 0`.
- **Their content changes per frame, in place.** `UploadSceneData` rewrites binding 1 (shadow maps) and binding 2 (the model matrices handle) on the same set every frame (`SceneDataManager.cpp:400-427`). A content-keyed store would have to mint a set per frame and reclaim the previous one — a cache that never hits, plus array support in the resolver. Strictly more work than today for no benefit.

But they may not keep their own pool either (D8). So they take the pool layer and keep writing their own descriptors. That is why the arena must offer a raw set at all.

### Delta-expression constraint discovered while writing this change

`openspec validate` refuses a `MODIFIED` requirement that omits any scenario the current spec has, and it matches scenarios **by name**: a renamed scenario reads as a dropped one. Two consequences for this change's deltas:

- The scenario `Physics bindings keep their 3-slot rotation` is retained although its premise is wrong twice over — physics passes no rotation depth at all today (`XPBDGpuSolver.cpp:282` and its siblings take the single-slot default), and after this change rotation depth no longer sizes descriptor-set storage. The name is now a query answered negatively, and the body says so. A future change that may legitimately drop it should rename the requirement instead.
- Every requirement *name* this change modifies in `rhi-compute-resource-binding` is kept byte-identical, because `compute-kernel-dispatch` removes those same requirements by name.

### Constraint from existing specs

`rhi-compute-resource-binding` forbids presentation vocabulary in RHI's public API, so the arena is expressed in terms of epochs, never frames.

### Constraints inherited from the changes around this one

- `rhi-module` is modified by `gpu-buffer-retirement` and by `compute-kernel-dispatch` as well. The `## MODIFIED Requirements` block for that capability here is based on the most recent delta text, and the arena sentence it carries is **identical** to the one in `compute-kernel-dispatch`, so archiving either order leaves the same text.
- Capabilities that `gpu-buffer-retirement` does not touch (`rhi-compute-resource-binding`, `rhi-directory-structure`) are based on the current spec text.
- `compute-kernel-dispatch` deletes `ComputeStage`, `ComputeResourceBinding` and `AllocateResourceBinding`. The requirement *names* this change modifies in `rhi-compute-resource-binding` are therefore kept byte-identical, so that change's `## REMOVED Requirements` block still matches them.

### The driver shapes the arena must serve

```
  FrameManager-driven (epoch opened before recording)      record-then-submit (headless tests)
  ---------------------------------------------------      -----------------------------------
  StartFrame -> BeginEpoch(w)                             record CB  -> acquire sets   (no epoch open: pinned)
  record CB   -> acquire sets (stamped w)                 BeginEpoch(w1) -> claims them
  SubmitFrame -> submit(w)                                submit(w1)
  ...                                                     record CB2 -> acquire sets   (stamped w1)
  StartFrame(frame+3) -> Complete(w)                      BeginEpoch(w2) -> claims them
                                                          Complete(w1) -> w1's sets become eligible
```

Both shapes exist today. The windowed hosts use the first: `FrameManager::StartFrame` opens the frame's epoch (`FrameManager.cpp:266`) and reports it only when that in-flight slot is reused, about an in-flight depth later (`:229-232`). The second is the headless test shape — `test/engine/headless/headless_compute_test.cpp:143-148` allocates a command buffer, begins it and records passes with no epoch opened anywhere.

`PhysicsApp` is **not** the second shape: it calls `StartFrame` before `BeginMainCommandBuffer` (`PhysicsApp.cpp:947-954`), so its sets are acquired under an open epoch. What distinguishes it is that it serialises each step (`WaitForFrameCompletion`), which is the shape that defeats age-driven release.

The design must not assume either shape.

## Goals / Non-Goals

**Goals**

- Give descriptor sets a bounded lifetime on every path, with no consumer-owned pool and no fixed per-consumer set budget.
- Make reuse the normal case: in steady state the arena should mint nothing, because the binding content of a kernel or a material is stable from frame to frame.
- Make reclamation a consequence of epoch completion, so nothing depends on frame counters or on a caller remembering to clean up.
- Keep the change invisible to physics: no physics file, shader, or binding call site changes here.

**Non-Goals**

- No descriptor-set churn reduction for the material path. Resolving interface names into a dense table once, so that per-frame hashing stops touching strings, is a separate follow-up.
- No automatic barriers; barrier placement stays manual and out of this change.
- `slot_count` uniform-buffer rotation still exists after this change. It is removed only by the later `compute-kernel-dispatch` and `physics-step-simplification` changes.
- No change to what a descriptor set contains, to shader interfaces, or to the reflection path.
- No change to the ImGui backend's own descriptor pool (D8).

## Decisions

### D1: One arena, two services

The arena owns every pool and hands out sets through two services that share pool growth, descriptor-set-layout reuse through `ImmutableResourceCache`, and debug naming:

```
  pool layer    AcquireRawSet(handle, name)                 -> a set the caller writes itself
  cache layer   Acquire(handle, flags, set_id, content)     -> the set for a content, resolved and written by the arena
  either way    ResolveLayout(description) -> handle        -> called once per layout, by the consumer (D14)
```

They are not two policies over one store: they differ in whether the arena knows what is in the set, and that difference decides everything else — whether a set can be keyed, shared, reused, or reclaimed (Context). Both take a `vk::DescriptorSetLayout` the arena resolved through the immutable resource cache, not a layout description to resolve on every call (D14).

**Alternatives rejected:**

- *Two separate classes.* They would duplicate pool growth, layout lookup and naming, and would leave two places that decide how a pool is grown.
- *One store for everything.* The cache layer can only key on content it wrote; `SceneDataManager` writes its own descriptors, so it has no key (Context).

### D2: The cache is keyed by layout, dynamic-offset flags and resolved binding content

```
  Key   = (vk::DescriptorSetLayout, dynamic-offset flags + set id, resolved binding entries)
  Entry = { vk::DescriptorSet set;  last_epoch;  the pool that served it;  acquisition order }
```

The layout component is the **`vk::DescriptorSetLayout` handle the immutable resource cache returned**, not an `SPLayout` object's address. The address is what today's per-instance cache keys on (`ShaderResourceBinding.cpp:92`), and it is tolerable only because one instance uses one or two layouts; a device-scoped store cannot use it, because an address is reused after its object dies and a stale entry would then answer for a different layout. Layout handles are content-addressed and never released, so they are stable keys. The caller obtains that handle from `ResolveLayout` and passes it in (D14), so the arena never rebuilds it on the acquisition path.

A hit refreshes `last_epoch` and the acquisition order, and returns the same set. **Reuse across epochs is the point**, not an accident: a physics kernel's binding content is stable from frame to frame, so after the first frame the arena should mint nothing at all.

What makes reuse safe is one invariant:

> a set is referenced only by command buffers of the epochs in which it was acquired

Together with the caller obligation in D10, `last_epoch` is an upper bound on the epochs that can reference an entry, so an entry is releasable exactly when `last_epoch <= the effective completed prefix` (D6).

**Alternatives rejected:**

- *Intern inside a per-epoch bucket and never reuse across epochs* (the original draft). Safe and simple, and it allows wholesale `vkResetDescriptorPool` release — but it re-mints every kernel's binding states every frame, forever, for content that is identical. The one thing it buys is a cheaper release path, and in steady state there is nothing to release.
- *A global cache with reference counting.* Reference counting re-derives what the epoch already knows, and one missed decrement reintroduces the leak.
- *One set per (binding, slot), rewritten in place when content changes.* An in-flight command buffer reads descriptors at execution time, not at record time, so a rewrite would be observed by the older frame — the hazard the current leak accidentally prevents.

### D3: The arena hashes and writes; `ShaderResourceBinding` resolves names

The arena can only key on content it produced, so the arena builds the descriptor writes on a miss. `ShaderResourceBinding` keeps its role — it holds the `name -> resource` map and maps those names onto the reflected layout — and hands the arena the resolved binding entries. It loses its own content cache, its `pool` parameter, and the layout derivation: the resolved `vk::DescriptorSetLayout` arrives from its caller (D14).

This keeps the per-frame cost at the hash-and-look-up it already was: a hit hashes the resolved entries and looks them up (today's `hash_current_interfaces`) and then does one store look-up, with no `SPLayout::GenerateLayoutBindings` on the path at all — that call and the resource-cache look-up behind it happen once per layout instead (D14).

### D4: The arena reads the completed prefix; it does not subscribe to it

The arena is not part of the submission protocol and needs one number: the largest watermark such that every watermark at or below it has been reported. It reads that from `EpochTracker::GetCompletedPrefix()` whenever it evaluates a reclamation.

**Nothing registers, and nothing is notified.** An earlier draft had the arena subscribe to prefix advancement. That mechanism does not exist: `EpochTracker`'s public surface is `BeginEpoch` / `ReportComplete` / `AbandonEpoch` / `ReleaseAllParked` / `Retire` / `RetireExact` plus getters, its park list holds `BufferAllocation` values rather than callbacks, and the retired `rhi-gpu-resource-retirement` capability describes no observer. A read is also sufficient, because both things the arena does with the prefix are demand-driven: it evaluates eligibility when an acquisition exceeds the budget (D6), so it needs the prefix at that moment and at no other.

**It is the prefix, not individual reports.** A single report for epoch `w` proves only that `w`'s submission finished; it says nothing about earlier epochs, which may still be executing command buffers that bind the same set. An entry is therefore eligible only when the *prefix* has passed its `last_epoch`, never when its own epoch happens to be reported. Since the prefix advances only through a contiguous run of reports, and `gpu-buffer-retirement` documents that it lags by about the in-flight depth, an entry acquired recently stays ineligible for that long — which is correct, because those epochs really may still be binding it.

### D5: Sets acquired with no open epoch are pinned until an epoch claims them

The headless and physics-test paths record their own command buffer and submit it themselves (`test/engine/headless/*`: `allocateCommandBuffers` + `begin`), so "acquire a set before any epoch exists" is a real case there, not an edge case.

Such an entry is marked **unclaimed** and is never evictable: the command buffer that bound it has not been submitted and will be submitted under whatever epoch comes next. When an epoch begins, it claims every unclaimed entry, stamping them with its watermark; from then on the ordinary rules apply. If no epoch ever opens, the entries stay pinned and are released when the arena is destroyed.

**`unclaimed` must be a distinct state, not `last_epoch = 0`.** Zero would compare as "already complete" and make the entry evictable immediately — the exact bug this rule exists to prevent.

### D6: One reclamation rule, and it is not the source TODO's LRU

```
  when over budget:
      evict eligible entries, least-recently-acquired first
      eligible = claimed  &&  last_epoch <= effective prefix          <- the guard
                effective prefix = max(GetCompletedPrefix(), idle watermark)   (D11)
      if no entry is eligible: exceed the budget
```

Four properties are deliberate:

- **Pressure-driven, not age-driven.** An earlier draft released an entry as soon as the epoch in which it was acquired completed ("use it or lose it, per epoch"). That degenerates on a fully serialised submitter: a host that completes each step's epoch inside the step would release everything every step and reuse nothing. Under a budget, entries persist while they keep being used and are discarded only when the cache needs the room.
- **Epoch-guarded.** The budget is a *desire*, not a licence: an entry still referenced by an outstanding epoch is never evicted. If every entry is ineligible, the arena exceeds the budget rather than corrupting an in-flight submission — so the budget is a soft cap, and the excess is bounded by the in-flight window.
- **There is no owner-driven release.** An earlier draft had a second trigger: an owner (a material, a scene, a camera) could release a set explicitly. It rested on the premise that `MaterialInstance::desc_set_cache[backbuffer]` is "a caller that holds a handle across frames". It is not — `GetDescriptor` reads the array on the statement after `UpdateGPUInfo` wrote it (`MaterialInstance.cpp:226-230`; call sites `CommandBuffer.cpp:274-279`, `SceneDataManager.cpp:454-459`), and no caller anywhere reads such a handle in a later epoch (Context, fact 4). With the premise gone, the trigger has no consumer, and it also made owner-driven entries exempt from the budget, so a forgotten release was unbounded. Since the pressure rule already reclaims exactly the right entries — an entry stops being refreshed when nobody requests its content any more, and is then reclaimed once the prefix passes it — the owner trigger is deleted. A release API would also have needed an owner identity in the key, because a device-scoped content key cannot say *whose* entry is being released.
- **Per-set release.** Returning sets individually requires `FREE_DESCRIPTOR_SET` on the pools and gives up the O(1) `vkResetDescriptorPool` that wholesale release allowed. That is the price of reuse, and it is small: in steady state almost nothing is released because almost nothing is minted.

**Alternative rejected:** an unguarded LRU (evict the least recently used entry when full), which is what the source's own `// XXX: use an LRU cache instead of unordered map here, to mitigate memory leak.` (`ShaderResourceBinding.cpp:55`) asks for. Its eviction decision is driven by capacity rather than by "is this still referenced", so it can free a set an in-flight command buffer binds. **Adding the guard is not an extra — the guard *is* the design**, and with it the policy is simply "least recently acquired among the eligible". The guard is what replaces the accidental protection the current leak provides.

### D7: The pool layer hands out raw sets, and reclaims none of them

```
  AcquireRawSet(layout, name) -> vk::DescriptorSet
```

The arena allocates from a pool that can serve the layout and returns the handle. It does not know, key, intern, or rewrite the set's descriptors; the caller writes them. Such a set is **not** reclaimed, because the arena cannot see whether its owner still holds the handle and cannot see the last epoch that bound it. That is the whole reason it is not a cache entry.

No release entry point is provided. Both consumers — `SceneDataManager` (three sets) and `CameraManager` (`FRAMES_IN_FLIGHT` sets) — are created once by `RenderSystem::Create` (`RenderSystem.cpp:109-111`) and live as long as the arena does; their sets are bounded by structural constants and today they die with their pool. A future consumer with a shorter lifetime can add a `ReturnSet(vk::DescriptorSet)` then; because raw sets are never interned, the handle itself is a unique identity, so no owner token is needed even then.

**The caller's obligation** (stated in the capability spec because it is the second thing a caller can get wrong) is:

> A set whose descriptors the caller writes MUST NOT be rewritten while a command buffer that binds it may still be executing.

`SceneDataManager` already satisfies this with its per-in-flight discipline: `FrameManager::StartFrame` waits the slot's `command_executed_fences[fif]` before resetting the command buffer and before the frame rewrites that slot's set (`FrameManager.cpp:219-223`). The obligation makes an existing implicit discipline explicit; it adds no code.

### D8: The arena owns every pool; no engine consumer creates, owns, or resets one

Four sites create a pool for engine bindings today: `ComputeStage` (one per stage), `MaterialLibrary` (one per material template), `SceneDataManager` and `CameraManager`. All four move onto the arena. Leaving any behind would make the capability's own requirement false at archive time, and would leave that path's unbounded growth in place.

One concrete consequence to migrate: `MaterialLibrary` uses the pool handle as a sentinel for "this material has material descriptors" (`if (b.descriptor_pool)` at `MaterialLibrary.cpp:176`). That check, and the two other places the same pool is used as that fact, must be replaced by a layout-derived condition (D13) rather than by another pool.

**The ImGui backend's pool is an exception, and the spec says so.** `GUISystem` creates a 1000-set pool with `eFreeDescriptorSet` and hands it to `ImGui_ImplVulkan_InitInfo` (`GUISystem.cpp:110-122`); ImGui allocates and frees those sets itself. It is not a pool for engine bindings, the arena cannot key or reclaim those sets, and no engine code path allocates from it. The capability's rule is therefore scoped to the sets the engine acquires for its own bindings, and the GUI pool is recorded as a deliberate exception with this reason.

### D9: On-demand growth replaces the fixed set budget

A request that a pool cannot serve causes the arena to create another pool (and a failed allocation with an out-of-pool-memory result is retried once against a fresh pool). The arena reports no maximum number of sets to its callers. The resident-entry budget of D6 is a separate, soft bound on the cache, not a per-consumer set budget. Pools are created with `eFreeDescriptorSet`, since sets are now released individually.

**Alternative rejected:** raising `MAX_COMPUTE_DESCRIPTORS_PER_POOL`. The value was never a guarantee, only a cliff; raising it moves the cliff.

### D10: No caller holds a set handle across epochs, and the memo arrays are deleted

The resident model is sound only under one obligation, which is now the capability's single caller contract:

> A set acquired from the cache layer must be re-acquired in **every** epoch whose command buffers use it, and its handle MUST NOT be held across epochs.

Both halves matter. Without the first, `last_epoch` under-states the referencing epochs and an entry can be evicted while a later command buffer still binds it. Without the second, a caller holds a handle the arena may already have released.

Today the obligation holds on every path — but only because the two places that store a handle read it back in the same statement sequence (Context, fact 4). Rather than rely on that, the storage goes away:

```
  UpdateGPUInfo(slot)  ->  struct { vk::DescriptorSet set; std::vector<uint32_t> dynamic_offsets; }
```

`ComputeResourceBinding::GetDescriptorSet` and `MaterialInstance::GetDescriptor` are deleted with the arrays they served. A caller cannot hold a handle across epochs because it is never handed one that outlives the call — the contract becomes structural instead of documented. This also fixes a latent defect: `ComputeResourceBinding::UpdateGPUInfo` is declared `const noexcept` (`ComputeResourceBinding.h:70`) while it can throw `vk::OutOfPoolMemoryError` from its allocation path, which would terminate the process; with the arena the throwing path returns a set and the declaration is revisited.

The acquisition points keep acquiring on every use, which is what satisfies the obligation: `Rhi::BindComputeResource` / `CommandBuffer::BindComputeResource` per dispatch, and `CommandBuffer::BindMaterial` per material bind. (The design's earlier draft named `ComputeKernel::Dispatch` here; that class arrives with `compute-kernel-dispatch`, and the obligation is unchanged, only renamed to its current call path.)

### D11: The device-idle broadcast lives on `DeviceContext`

A device-wide idle wait proves something stronger than any report: every submission that could reference anything has finished. It must not release the live cache — that would defeat the cache on a fully serialised submitter — but it is proof that nothing in the cache is referenced any more, so it should at least make entries *eligible*.

The tracker does not express that. `RenderSystem::WaitForIdle` waits the device and calls `EpochTracker::ReleaseAllParked` (`RenderSystem.cpp:176-183`), which clears parked buffer allocations and deliberately does **not** touch the completed prefix; the capability documents that idle "is not authority to discard resources that are still live". Rather than give the tracker a way to claim reports it never observed, the broadcast is one call that covers both ledgers:

```
  DeviceContext::WaitForIdle() {
      device.waitIdle();                     // the fact
      epoch_tracker->ReleaseAllParked();      // what was already there
      descriptor_arena->OnDeviceIdle();       // what the arena adds
  }
```

`RenderSystem::WaitForIdle` delegates to it. The entry point performs the wait itself, so "notify without waiting" cannot be written.

`DescriptorArena::OnDeviceIdle` records a watermark and releases nothing:

```
  m_idle_watermark = max(m_idle_watermark, tracker.GetNewestWatermark());
  effective_prefix = max(tracker.GetCompletedPrefix(), m_idle_watermark)
```

Recording a **watermark rather than a flag** is required: an entry acquired after the idle wait must stay ineligible, and its watermark is greater than `m_idle_watermark` by construction, whereas a flag would mark new entries eligible too. This makes the capability's sentence "idle makes entries eligible but reclamation still requires budget pressure" true without inventing a state the tracker never observed.

### D12: Declaration order in `DeviceContext`: the arena after the device, before the tracker

```
  declaration order:  DeviceInterface, AllocatorState, ImmutableResourceCache, DescriptorArena, EpochTracker
  destruction order:  EpochTracker -> DescriptorArena -> ImmutableResourceCache, AllocatorState -> DeviceInterface
```

The only ordering the arena strictly requires is that it be declared **after** `DeviceInterface`: its pools hold device objects and must be destroyed while the device is alive. It is placed before `EpochTracker` so that the members read in dependency order and so that the tracker, which the arena reads, is torn down before the arena that reads it — a teardown path that consults eligibility cannot then find a destroyed ledger. The earlier draft needed the arena declared before the tracker for a different reason ("the tracker must not outlive its observer"); that reason is gone with the observer (D4).

**Alternative rejected:** rely on the arena being destroyed before the device by convention rather than by declaration order, and make its teardown tolerate a dead device. A freed-pool-after-device error is precisely what declaration order exists to prevent, and "tolerate" would mean silently skipping the release.

### D13: The material path's "has material data" is computed once, from the reflected layout

The pool was used as a fact in three places: the branch sentinel in `MaterialLibrary::CreatePipeline` (`MaterialLibrary.cpp:176-191`), `MaterialTemplate::GetDescriptorPool()` (`MaterialTemplate.cpp:186-188`), and `MaterialTemplate::HasMaterialData()` (`MaterialTemplate.cpp:193-195`) — which is the gate `MaterialInstance.cpp:172` and `CommandBuffer.cpp:272` rely on. The fact they all encode is "the reflected layout declares set 2". It is computed once for the template and stored, because the natural expression — `reflected.GenerateLayoutBindings(2, true, false).empty()` — allocates a vector and must not run per frame. The same rewrite removes the log at `MaterialTemplate.cpp:164-166`, which tests the pool before assigning it and therefore always fires.

`ComputeStage` has the mirror-image problem: it creates its descriptor set layout with `createDescriptorSetLayoutUnique` directly (`ComputeStage.cpp:48`) instead of through the immutable resource cache, while the set that `ShaderResourceBinding` allocates uses the cache's equal-content layout (`:185-187`). The pipeline layout and the allocation layout are therefore never asserted equal — a hazard the previous change recorded rather than fixed. Creating the stage's layout through the cache removes the hazard instead of inheriting it, and it is what makes the layout handle usable as a cache key (D2).

Two further hazards are carried forward unchanged, recorded so they are not mistaken for consequences of this change: the duplicate `// FIXME: Dynamic offset order might not be correct.` sites (`MaterialInstance.cpp:189`, `ComputeResourceBinding.cpp:126`), and the fact that `MaterialInstance::m_pass_infos` is keyed by `const MaterialTemplate*` while `MaterialLibrary::Instantiate` clears the table that owns those templates (`MaterialLibrary.cpp:235`).

### D14: The caller resolves the layout once and passes the handle

D2 makes the key's first component a `vk::DescriptorSetLayout`, and the only way to obtain one is to hand a description to `ImmutableResourceCache`. That makes the layout **description** — and therefore `SPLayout::GenerateLayoutBindings` — an input to key construction:

```
  SPLayout::interfaces
    -> GenerateLayoutBindings(set_id, flags)  -> std::vector<vk::DescriptorSetLayoutBinding>
    -> vk::DescriptorSetLayoutCreateInfo      -> ImmutableResourceCache -> vk::DescriptorSetLayout
    -> the key's first component
```

Resolving that on the acquisition path would move the expensive step from "once per layout" to "every material bind and every dispatch" — exactly the cost the change set out to avoid — and it would make the pipeline-layout identity depend on the resource cache de-duplicating two separately built descriptions. So the resolution is hoisted to where the description is already known:

```
  ResolveLayout(description) -> vk::DescriptorSetLayout     once per layout, by the consumer
  Acquire(handle, flags, set_id, content)                   every acquisition
```

`ResolveLayout` still resolves **through `ImmutableResourceCache`**, so the arena remains the one thing that turns a description into the shared layout object, and it records the per-descriptor-type requirement of every layout it resolves — it needs that to size a pool, and a `vk::DescriptorSetLayout` cannot be queried for its bindings afterwards.

The identity of the layout a pipeline layout is built over and the layout a set is allocated against therefore becomes structural rather than incidental: a consumer resolves one handle, builds its pipeline layout over it, and hands that same handle to the arena. `ComputeStage` already cached exactly this handle (`GetDescriptorSetLayout()`); `MaterialTemplate` carries it too now (D13).

**Alternative rejected:** keep resolving on the acquisition path. It satisfies the key requirement, but pays `GenerateLayoutBindings` per acquisition, and leaves the two-layouts-are-equal guarantee resting on the resource cache rather than on the code holding one handle.

## Risks / Trade-offs

- **[An entry is evicted while a recorded command buffer still binds it]** → Mitigation: eviction is guarded by `claimed && last_epoch <= effective prefix` (D6), the re-acquire obligation that makes `last_epoch` an upper bound is a spec requirement (D10), and the memo arrays that would have let a caller hold a handle are deleted.
- **[A caller caches a set handle across epochs and uses a released set]** → Mitigation: the contract is a spec requirement, and it is structurally enforced — the acquisition API returns the set inside a call result rather than as storable state (D10).
- **[A fully serialised submitter re-mints everything]** → Mitigation: the budget, not the epoch, drives release (D6); the physics-app shape is an explicit test fixture, since it is the shape that broke the earlier age-driven rule.
- **[Material sets are now reclaimed lazily rather than on owner destruction]** → Accepted: the alternative was an owner identity in the key, a deferred-release path and a budget exemption, for a call site that never held a handle across frames. The bound is unchanged in kind and better in practice — entries are now inside the budget instead of outside it — but the moment of reclamation moves from "the instance died" to "the next over-budget acquisition after the prefix passed the entry", so the material-churn fixture must apply pressure to observe it.
- **[Losing the per-consumer 128-set budget lets one consumer's burst starve others]** → Mitigation: the arena grows pools on demand rather than partitioning; the old budget was a cliff, not a guarantee; pool and set counts are observable for regression tracking.
- **[Cache growth if binding content churns every frame]** → Mitigation: the soft budget bounds the resident set, ineligible entries are the only ones allowed to exceed it, and a debug counter reports both the resident count and the number of entries held back by the guard.
- **[The completed prefix lags, so entries stay ineligible longer than they are actually in use]** → Accepted: `gpu-buffer-retirement` documents that the prefix lags by about one in-flight depth because a frame is reported only when its slot is reused. The consequence here is that the resident cache holds a window of roughly that many epochs' worth of set content, and that eviction under pressure may have nothing eligible to release. The budget is soft for exactly this reason; the counter of entries held back by the guard makes the condition visible, and the idle broadcast (D11) shortens it after any device-wide wait.
- **[Teardown order inverted: the tracker or the cache destroyed before the arena]** → Mitigation: declaration order in `DeviceContext` (D12), plus a device-idle teardown test under the validation layer that leaves entries resident.
- **[A raw-set consumer rewrites a set an in-flight submission binds]** → Mitigation: the obligation is a spec requirement (D7), and the only two consumers already satisfy it through the per-in-flight fence that `FrameManager` waits before it resets the slot's command buffer.
- **[Material path behaviour changes more than expected when its pool disappears]** → Mitigation: the three pool-derived facts are identified up front (D13) and replaced by one cached layout-derived boolean; the material-churn test asserts the live count returns to its baseline once pressure is applied.
- **[Release becomes silent when a submitter stops reporting completions]** → Mitigation: entries simply stop becoming eligible, which the outstanding-watermark counter added by `gpu-buffer-retirement` already exposes; the arena's resident counter makes the growth visible.
- **[A future short-lived raw-set consumer leaks]** → Accepted: no such consumer exists, the sets are bounded by structural constants, and adding `ReturnSet` later needs no owner identity because raw sets are never interned (D7).
- **[A caller hands the arena a layout it never resolved]** → Mitigation: the contract in the capability spec says the layout comes from the arena, the arena asserts on a handle it does not know, and the only producers are `ComputeStage`, `MaterialTemplate`, `SceneDataManager` and `CameraManager`, all of which resolve through it (D14).

## Migration Plan

1. Introduce the arena with its pool layer, its layout resolution (D14), its cache layer, the pinning rule (D5) and the guarded pressure eviction (D6); wire `DeviceContext` with the declaration order from D12 and the idle broadcast from D11. No behaviour change yet for any consumer.
2. Move the compute path in: `ComputeStage` stops creating a pool and resolves its layout through the arena (which resolves it through the immutable resource cache); its accessor now hands that handle to the binding. `ComputeResourceBinding` acquires from the arena and returns the set with its offsets, and its per-slot handle array is deleted.
3. Move the material path in: `ShaderResourceBinding` becomes the name resolver in front of the arena, loses its own cache, its `pool` parameter and its layout derivation; `MaterialInstance` returns the set with its offsets and loses its handle array and `GetDescriptor`; `MaterialTemplate` / `MaterialLibrary` lose the pool, carry the resolved layout, replace the three pool-derived facts with one cached layout-derived boolean, and the dead pool vocabulary is deleted.
4. Move `SceneDataManager` and `CameraManager` onto the pool layer (D7, D8), so that no consumer in the repository creates a pool except the ImGui backend (D8).
5. Add the verification fixtures: steady-state zero minting, reuse across epochs on both driver shapes, growth past the old budget, eviction under pressure with entries still outstanding, pinning, and material churn under pressure.
6. Run the full suite and validate.

Rollback is per-step. Steps 2, 3 and 4 are independent of one another once step 1 lands: any one can be reverted while the others stay, because they share only the arena's pool bookkeeping.

## Open Questions

- **The resident budget's default value.** The arena needs *a* number; it is a safety bound rather than a tuning knob, and normal operation should never reach it. The counters from step 5 are the input, so the value can be settled during implementation without changing the specs or the task breakdown.
- **Precomputing the material path's name-to-slot resolution.** It is the churn-reduction work this change explicitly excludes; it becomes natural once the arena owns acquisition, but it changes hashing behaviour and deserves its own change.
