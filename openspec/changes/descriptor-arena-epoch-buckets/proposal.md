## Why

Descriptor sets are allocated ad hoc by independent paths, cached in maps that are never pruned, and paid for out of pools that are never reset:

- `ShaderResourceBinding::impl::descriptor_sets` is a two-level content-hash cache allocated on a miss (`ShaderResourceBinding.cpp:190-192`) and never erased. The source itself carries `// XXX: use an LRU cache instead of unordered map here, to mitigate memory leak.` (`:55`) and the header admits that rebinding "can lead to memory leak if too frequent" (`ShaderResourceBinding.h:31-34`).
- `ComputeStage` creates one pool per stage with `MAX_COMPUTE_DESCRIPTORS_PER_POOL = 128` (`ComputeStage.cpp:20`, `:65-68`), never resets it, and calls `allocateDescriptorSets` with no error handling — pool exhaustion is a hard 128-set budget. Physics runs roughly forty stages.
- `MaterialTemplate` / `MaterialLibrary` create another pool family with the same fixed budget (`MaterialLibrary.cpp:126-132`), and `SceneDataManager` (`:140`) and `CameraManager` (`:88`) create two more.

The content hash covers the bound `(name, handle, offset, size)` triples, so **every buffer reallocation mints a descriptor set that is never released**. Physics reallocates on every geometry change, so the compute path exhausts its budget first; the material path leaks more slowly because its content only changes when textures or buffers are rebound.

The leak is currently **load-bearing**: because sets are never freed, an in-flight command buffer never observes a freed or rewritten set. So the goal is not merely "stop leaking" but "reclaim only what is provably unreferenced" — which is why this change follows `gpu-buffer-retirement`, whose epochs make release provable.

This is the second of four changes; it makes descriptor-set lifetime bounded without waiting for the compute dispatch API to be replaced.

## What Changes

- **New device-scoped `Rhi::DescriptorArena`, owned by `DeviceContext`.** Sets are kept **resident** in one content-keyed store and **reused across epochs**, because a kernel's or a material's binding content is stable from frame to frame: in steady state the arena should mint nothing. The source's own `LRU` TODO becomes implementable — what was missing was never the eviction policy but knowing when eviction is safe.
- **Two release triggers rather than one policy**, because the consumer families differ: cache pressure for per-dispatch compute bindings (nobody is left to say when a set is done), and an explicit owner release for material, scene and camera state (the owner holds the handle across frames and knows when it dies).
- **Eviction is pressure-driven and epoch-guarded.** Entries are released only when the arena is over a resident budget, and only among entries whose recorded epoch is at or below the completed watermark; the budget is therefore a **soft** cap — the arena exceeds it rather than releasing a set an outstanding submission may reference.
- **Sets acquired while no epoch is open are pinned** until an epoch claims them, and are never evictable while unclaimed: the command buffer that bound them has not been submitted yet.
- **A stated contract for pressure-driven sets**: they must be re-acquired in every epoch that uses them, and their handles must not be cached across epochs. Callers that need to hold a handle across frames use the owner-driven trigger.
- **BREAKING:** `ShaderResourceBinding::GetDescriptorSet` loses its `pool` parameter; `ComputeStage::GetDescriptorPool` and `MaterialTemplate::GetDescriptorPool` are removed. **`ComputeStage`, `MaterialTemplate`/`MaterialLibrary`, `SceneDataManager` and `CameraManager` all stop owning a descriptor pool** — no consumer creates, owns or resets one.
- **The arena observes exactly one thing from `gpu-buffer-retirement`**: the completed watermark advancing. It is not part of the submission protocol, and a device-idle wait does not discard its cache.
- No compute dispatch API, shader, or barrier placement changes in this change.

## Capabilities

### New Capabilities

- `rhi-descriptor-arena`: the arena's contract — the resident content-keyed store and cross-epoch reuse, the two release triggers, the epoch guard and the soft budget, the pinning rule for sets acquired outside an epoch, the re-acquisition contract, on-demand pool growth instead of a fixed set cap, and descriptor-set-layout reuse.

### Modified Capabilities

- `rhi-compute-resource-binding`: the binding path no longer pre-allocates descriptor-set slots or supplies a pool; rotation depth still governs the per-slot uniform-buffer slices, and descriptor sets are obtained from the arena.
- `rhi-module`: the module inventory gains the descriptor arena, and `ComputeStage`'s pipeline-creation contract no longer includes creating a descriptor pool.
- `rhi-directory-structure`: the arena's source files are added to the `Resource/` group's file list.

## Impact

**Code**
- `engine/Rhi/Resource/` — new `DescriptorArena.*`; `ImmutableResourceCache` unchanged but now shared as the layout source.
- `engine/Rhi/Pipeline/` — `ComputeStage.*` (pool removal), `ComputeResourceBinding.*` (arena acquisition), `ShaderResourceBinding.*` (bounded cache, no pool parameter, release through the arena).
- `engine/Rhi/Device/` — `DeviceContext` owns the arena, declared before the retirement tracker so the tracker cannot outlive its observer.
- `engine/Render/Pipeline/Material/` — `MaterialInstance.cpp` call site; `MaterialTemplate.*` and `MaterialLibrary.cpp` lose the per-template pool and its naming.
- `engine/Render/RenderSystem/` — `SceneDataManager.*` and `CameraManager.*` move their pools onto the arena.
- `engine/Rhi/Submission/` — the completed-watermark advancement the arena registers with (introduced by `gpu-buffer-retirement`).

**API**
- Breaking: `ShaderResourceBinding::GetDescriptorSet`, `ComputeStage::GetDescriptorPool`, `MaterialTemplate::GetDescriptorPool`. All other changes are additive.

**Tests**
- A steady-state fixture must show the minted-set counter stop increasing after the first epoch; a fully-serialised fixture (the physics-app shape, which is what defeats age-driven release) must show reuse across epochs rather than re-minting; an eviction fixture must show ineligible entries retained and the budget exceeded rather than violated; a material-churn fixture must show sets released on instance destruction; a growth test must acquire past the old 128-set budget without failure.
