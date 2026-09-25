## Why

Descriptor sets are allocated ad hoc by independent paths, cached in maps that are never pruned, and paid for out of pools that are never reset:

- `ShaderResourceBinding::impl::descriptor_sets` is a two-level content-hash cache allocated on a miss (`ShaderResourceBinding.cpp:189-192`) and never erased. The source itself carries `// XXX: use an LRU cache instead of unordered map here, to mitigate memory leak.` (`:55`) and the header admits that rebinding "can lead to memory leak if too frequent" (`ShaderResourceBinding.h:30-33`).
- `ComputeStage` creates one pool per stage with `MAX_COMPUTE_DESCRIPTORS_PER_POOL = 128` (`ComputeStage.cpp:20`, `:65-68`), never resets it, and calls `allocateDescriptorSets` with no error handling — pool exhaustion is a hard 128-set budget. Physics runs roughly forty stages.
- `MaterialTemplate` / `MaterialLibrary` create another pool family with the same fixed budget (`MaterialLibrary.cpp:126-131`), and `SceneDataManager` (`:251-256`) and `CameraManager` (`:57-60`) create two more. `GUISystem` (`:110-113`) creates a fifth for the ImGui backend, which owns and frees those sets itself.

The content hash covers the bound `(name, handle, offset, size)` triples, so **every buffer reallocation mints a descriptor set that is never released**. Physics still reallocates whenever a requested size differs — `stable-buffer-identity` made the buffer *object* survive a reallocation, and geometric growth arrives only with `physics-step-simplification` — so the compute path is the one that exhausts its budget first; the material path leaks more slowly because its content only changes when textures or buffers are rebound.

The leak is currently **load-bearing**: because sets are never freed, an in-flight command buffer never observes a freed or rewritten set. So the goal is not merely "stop leaking" but "reclaim only what is provably unreferenced" — which is why this change follows `gpu-buffer-retirement`, whose epochs make release provable.

This is the second of four changes; it makes descriptor-set lifetime bounded without waiting for the compute dispatch API to be replaced.

## What Changes

- **New device-scoped `Rhi::DescriptorArena`, owned by `DeviceContext`.** It owns every pool and hands out sets through **two services**: a **pool layer** that allocates a set for a layout without knowing its contents, and a **content-keyed cache layer** built on it that resolves and writes descriptors. Sets are kept **resident** and **reused across epochs**, because a kernel's or a material's binding content is stable from frame to frame: in steady state the arena should mint nothing. The source's own `LRU` TODO becomes implementable — what was missing was never the eviction policy but knowing when eviction is safe.
- **One reclamation rule: a soft budget under an epoch guard.** Entries are released only when the arena is over a resident budget, and only among entries whose recorded epoch is at or below the completed **prefix** — a report for an entry's own epoch is not enough, since earlier epochs may still bind the same set; the budget is therefore a **soft** cap — the arena exceeds it rather than releasing a set an outstanding submission may reference. An unguarded LRU is not enough: the guard is what replaces the protection the current leak accidentally provides.
- **There is no owner-driven release.** Every caller that stores a set handle reads it back in the same call that refreshed it, and no caller holds one across an epoch, so an explicit owner release has no consumer — and no owner identity is needed in a device-scoped content key. Reclamation is therefore uniform: an entry stops being refreshed when nobody requests its content any more, and is then reclaimed once the prefix has passed it.
- **No caller may hold a set handle across epochs.** The acquisitions that store handles today (`ComputeResourceBinding::descriptor_sets`, `MaterialInstance::desc_set_cache`) are deleted, and `UpdateGPUInfo` returns the set together with its dynamic offsets — so the contract is structural rather than documented.
- **Sets acquired while no epoch is open are pinned** until an epoch claims them, and are never evictable while unclaimed: the command buffer that bound them has not been submitted yet.
- **The pool layer serves sets the caller writes.** `SceneDataManager` and `CameraManager` keep their per-in-flight sets and keep writing their own descriptors — an array of shadow-map samplers and a per-frame descriptor rewrite do not fit a content-keyed store — but they no longer create or own a pool. Such a set is never reclaimed by the arena, and the caller carries the obligation not to rewrite it while a command buffer that binds it may still be executing.
- **The device-idle broadcast is one call on `DeviceContext`.** `WaitForIdle` waits the device, releases the retirement facility's parked allocations and tells the arena that everything issued so far is complete — which makes entries eligible without releasing any of them. The tracker's own contract is untouched.
- **BREAKING:** `ShaderResourceBinding::GetDescriptorSet` loses its `pool` parameter, and `ComputeResourceBinding::GetDescriptorSet` / `MaterialInstance::GetDescriptor` are deleted along with their handle arrays. `ComputeStage::GetDescriptorPool` and `MaterialTemplate::GetDescriptorPool` are removed. `ComputeStage`, `MaterialTemplate`/`MaterialLibrary`, `SceneDataManager` and `CameraManager` all stop owning a descriptor pool, and `MaterialTemplate` computes its "has per-material data" fact once from the reflected layout instead of reading it off the pool.
- **`ComputeStage` creates its descriptor set layout through the immutable resource cache**, as `MaterialLibrary` already does, so the pipeline's layout and the layout a set is allocated against are the same object — which also removes a hazard the previous change recorded rather than fixed.
- **The ImGui backend's pool is a stated exception.** It is not a pool for engine bindings, and the GUI library allocates and frees those sets itself.
- **The arena reads the tracker's completed prefix; it does not subscribe to it.** The arena is not part of the submission protocol and needs no event: both things it does with the prefix are demand-driven.
- No compute dispatch API, shader, or barrier placement changes in this change.

## Capabilities

### New Capabilities

- `rhi-descriptor-arena`: the arena's contract — the pool layer for sets the caller writes and the content-keyed cache layer for sets the arena resolves, cross-epoch reuse, the re-acquisition contract, the epoch guard and the soft budget, the pinning rule for sets acquired outside an epoch, on-demand pool growth instead of a fixed set cap, and descriptor-set-layout reuse.

### Modified Capabilities

- `rhi-compute-resource-binding`: the binding path no longer pre-allocates descriptor-set slots, supplies a pool, or stores set handles; `UpdateGPUInfo` returns the set together with its dynamic offsets, rotation depth governs only the per-slot uniform-buffer slices, and descriptor sets are obtained from the arena.
- `rhi-module`: the module inventory gains the descriptor arena, and `ComputeStage`'s pipeline-creation contract no longer includes creating a descriptor pool and now obtains its descriptor set layout from the immutable resource cache.
- `rhi-directory-structure`: the arena's source files are added to the `Resource/` group's file list.

## Impact

**Code**
- `engine/Rhi/Resource/` — new `DescriptorArena.*`; `ImmutableResourceCache` unchanged but now the layout source for both services.
- `engine/Rhi/Pipeline/` — `ComputeStage.*` (pool removal, layout through the cache), `ComputeResourceBinding.*` (arena acquisition, returned set, handle array deleted), `ShaderResourceBinding.*` (becomes the name resolver; no own cache, no pool parameter).
- `engine/Rhi/Device/` — `DeviceContext` owns the arena, declares it before the retirement tracker, and gains `WaitForIdle()` as the single device-idle broadcast.
- `engine/Render/Pipeline/Material/` — `MaterialInstance.cpp` and `.h` call sites; `MaterialTemplate.*` and `MaterialLibrary.cpp` lose the per-template pool and its naming, and the pool-derived "has material data" fact becomes a cached layout-derived boolean.
- `engine/Render/RenderSystem/` — `SceneDataManager.*` and `CameraManager.*` obtain their existing sets from the arena's pool layer instead of a pool they own.
- `engine/Rhi/Submission/` — the completed-prefix reader the arena consults (introduced by `gpu-buffer-retirement`); no change to the tracker.

**API**
- Breaking: `ShaderResourceBinding::GetDescriptorSet` (pool parameter), `ComputeResourceBinding::GetDescriptorSet`, `MaterialInstance::GetDescriptor`, `ComputeStage::GetDescriptorPool`, `MaterialTemplate::GetDescriptorPool`, and the return type of both `UpdateGPUInfo` overloads.
- Additive: `DescriptorArena`, `DeviceContext::WaitForIdle`.
- Untouched by design: `GUISystem`'s ImGui backend pool.

**Tests**
- A steady-state fixture must show the minted-set counter stop increasing after the first epoch; a fully-serialised fixture (the physics-app shape, which is what defeats age-driven release) must show reuse across epochs rather than re-minting; an eviction fixture must show ineligible entries retained and the budget exceeded rather than violated; a pinning fixture must show sets acquired with no epoch open surviving pressure; a material-churn fixture must show sets reclaimed once pressure is applied; a growth test must acquire past the old 128-set budget without failure.
