# Tasks: descriptor-arena-resource-identity

## 1. Identity infrastructure (commit 1)

- [x] 1.1 Create `engine/Rhi/Device/BindingRef.h` with `VulkanIdentity` (monotonic, never reused) and `BufferRef` / `ViewRef` / `SamplerRef` / `DescriptorSetLayoutRef` (`{handle, VulkanIdentity}`, identity-only equality); verify the project builds and a reference cannot be constructed from a bare handle (no such constructor exists)
- [x] 1.2 Mint an identity in `BufferAllocation` / `ImageAllocation` construction (the sole `vmaCreateBuffer` path) and expose it through `DeviceBuffer`; verify each allocation in a create/destroy loop receives a distinct identity and a reallocated buffer's replacement receives a new one
- [x] 1.3 Mint an identity per created image view in `Texture::GetImageView`'s view cache; verify two views of one texture hold different identities and repeated queries of one range return the same identity
- [x] 1.4 Mint identities in `ImmutableResourceCache` for samplers and descriptor-set layouts and expose ref-returning accessors beside the existing getters; verify a repeated `GetSampler` of one description returns one identity

## 2. Binding flows and arena keying (commit 1)

- [x] 2.1 Convert `ComputeKernelResource::BufferEntry` / `ImageEntry` to carry references, resolved from the wrapper in the implicit constructor and `Buffer()` / `Image()`; verify all existing dispatch call sites compile unchanged
- [x] 2.2 Convert `ShaderResourceBinding` to store `{wrapper pointer, offset/size window}` and resolve handle and identity at `GetDescriptorSet`; document the "a bound resource is neither moved nor destroyed while bound" contract in the header; verify a buffer whose storage is replaced after binding resolves to the replacement
- [x] 2.3 Convert `ResolvedBinding` to carry references and re-key `operator==` / `HashContent` on object identity (windows stay in the binding, not the reference); verify no comparison or hash over bound resources uses a raw handle value
- [x] 2.4 Cross the arena boundary with refs: `ResolveLayout` returns a layout reference, `Acquire` / `AcquireRawSet` accept references, the per-layout requirements map keys on layout identity; update the storing callers (`ComputeKernel`, `MaterialLibrary`, `CameraManager`, `SceneDataManager`) and add a reference-returning accessor on `MaterialTemplate`; verify the project builds with public raw getters unchanged

## 3. Insert-collision fix (commit 2)

- [x] 3.1 Chain resident entries per key and select the entry whose stored content is exactly the requested content, so insertion cannot fail and no request is answered with another content's set; verify no code path returns a set whose stored content differs from the request and the registry tests still pass

## 4. Handle-recycling stress test (commit 3)

- [x] 4.1 Add a headless stress test with a buffer-churn scenario: repeatedly create, bind, dispatch and destroy buffers at randomised sizes, then verify dispatch results stay correct across iterations
- [x] 4.2 Extend the stress test with a texture/view-churn scenario: repeatedly create, bind, acquire and destroy textures and verify each new texture mints its own set (no compute shader in the tree binds a storage image, so the check is on the acquired descriptor content rather than on read-back pixels)
- [x] 4.3 Verify the stress test passes in Debug and Release, with and without the validation layer, over repeated runs

## 5. Verification gate

- [x] 5.1 Verify `gpu_compact_unique_test`, `gpu_radix_multiblock_test` and `xpbd_reduce_capacity_test` pass in Debug and Release, with and without the validation layer
- [x] 5.2 Verify the full ctest suite passes in Debug and Release with no regressions (70/70 pass in both, with `asset_path_test` excluded: that test exercises Asset/Core only — none of which this change touches — and fails independently of it, hanging on an assertion dialog in Debug and exiting 0xc0000409 in Release)
- [x] 5.3 Land the work as three commits (identity fix, insert-collision fix, stress test) and verify each commit builds on its own
