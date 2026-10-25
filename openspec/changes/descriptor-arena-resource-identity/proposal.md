# Proposal: descriptor-arena-resource-identity

## Why

The descriptor arena's content-keyed cache identifies bound resources by their raw `VkBuffer` / `VkImageView` / `VkSampler` handle values. Vulkan drivers recycle handle values after destruction, so a resource created after another one died can present the exact same "content" as a stale resident entry: the arena then returns a descriptor set whose descriptors reference destroyed resources (undefined behaviour — on NVIDIA the descriptor bakes the device address at write time, producing reads of zero and lost writes). This is the confirmed root cause of `gpu_compact_unique_test`, `gpu_radix_multiblock_test` and `xpbd_reduce_capacity_test` passing under the validation layer and failing without it: the layer's allocation pattern masks the handle recycling.

## What Changes

- Introduce a never-reused `ObjectId` minted when a descriptor-relevant Vulkan object is created — buffer allocations, image views, samplers and descriptor-set layouts.
- Introduce typed binding references (`BufferRef` / `ViewRef` / `SamplerRef` / `DescriptorSetLayoutRef`, each a handle plus its `ObjectId`) that can only be constructed from the RHI wrapper that owns the object; raw handles can no longer enter a binding identity.
- Binding structures (`ComputeKernelResource`, `ShaderResourceBinding`'s binding map, `ResolvedBinding`) carry these references instead of raw handles; cache equality and hashing use object identity, so recycled handle values can never alias two distinct resources.
- Fix the cache-insert collision path: two distinct contents that share a content hash remain distinct entries, and a request is never answered with a set written for different content.
- `ShaderResourceBinding` resolves handles and identity at acquisition instead of snapshotting raw handles at bind time, so a reallocated buffer self-heals; the documented contract becomes "a bound resource is neither moved nor destroyed while bound".
- No debug-only behavioural branches: the fix behaves identically in Debug and Release.

## Capabilities

### New Capabilities

- `rhi-resource-identity`: never-reused object identity and typed binding references for descriptor-relevant Vulkan resources, minted at object creation and carried through binding flows.

### Modified Capabilities

- `rhi-descriptor-arena`: the resident cache keys on resource identity rather than raw handle values; distinct contents sharing a content hash stay distinct entries and are never conflated.

## Impact

- **engine/Rhi**: new `Rhi/Device/ResourceRef.h`; identity minting in `BufferAllocation` / `ImageAllocation`, `Texture`'s image-view cache and `ImmutableResourceCache`; consumption in `ComputeKernel`, `ShaderResourceBinding` and `DescriptorArena`. `DescriptorArena::Acquire` / `AcquireRawSet` / `ResolveLayout` cross the arena boundary as references.
- **engine/Render**: `MaterialTemplate` / `MaterialLibrary` / `CameraManager` / `SceneDataManager` store layout references from `ResolveLayout`; a reference-returning layout accessor is added beside the existing getter. Public raw-handle getters keep their names and signatures (record-time use only).
- **Not affected**: command recording (copies, barriers, index/vertex binds), ImGui interop, the pool layer's set lifetime rules, epoch-based reclamation.
- **Verification**: the three failing tests across Debug/Release × validation-layer on/off, a new handle-recycling stress test (buffer churn and texture/view churn), and the full ctest suite.
