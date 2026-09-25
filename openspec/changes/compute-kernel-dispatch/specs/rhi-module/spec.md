# rhi-module

## MODIFIED Requirements

### Requirement: Rhi hosts the generic GPU infrastructure types

`Rhi` SHALL contain the following types, moved from `engine/Render/` without semantic changes: `DeviceInterface`, `AllocatorState`, `MemoryTypes` / `MemoryAllocation`, `DeviceBuffer`, `ComputeBuffer`, `StructuredBuffer`, `StructuredBufferPlacer`, `Texture`, `ImageTexture`, `TextureSubresourceView`, `ImageUtils`, `ImmutableResourceCache`, `SubmissionHelper`, `ShaderResourceBinding`, `ShaderParameterLayout`, `ShaderInterface`, `MemoryAccessTypes`, `PipelineEnums`.

`Rhi` SHALL additionally own the device-scoped GPU resource retirement facility and its supporting types: the submission epoch tracker — which is itself the sole recipient of retired buffer allocations. It resides in `Rhi` because it depends on the device and allocator only, and both `Render` and `Physics` use it equally.

`Rhi` SHALL NOT provide a shared-ownership (reference-counted) buffer factory. A buffer whose lifetime must outlive the component that created it is uniquely owned by whoever owns that lifetime, and is referenced elsewhere through a raw pointer.

`Rhi` SHALL additionally own the device-scoped descriptor arena: the single owner of descriptor pools, which keeps acquired sets resident and reusable across submission epochs and reclaims them under one of two triggers — cache pressure for per-dispatch compute bindings and an explicit owner release for long-lived material, scene and camera state. The arena resides in `Rhi` because both `Render` (materials, scene data, cameras) and compute consumers use it equally, and neither may own a pool.

`Rhi` SHALL additionally own the compute kernel facility and its dispatch surface. It resides in `Rhi` because it depends on the device, the allocator and the arena only, and both `Render` and `Physics` use it equally.

The types `ComputeStage`, `ComputeResourceBinding` and the `ComputeHelpers` free functions SHALL NOT exist in `Rhi`: the kernel facility supersedes them.

#### Scenario: Buffer types available from Rhi

- **WHEN** a client includes `Rhi/ComputeBuffer.h`
- **THEN** `Engine::Rhi::ComputeBuffer` and its `CreateUnique(allocator, ...)` factory are available

#### Scenario: Texture types available from Rhi

- **WHEN** a client includes `Rhi/Texture.h`
- **THEN** `Engine::Rhi::Texture` and the `Engine::Rhi::ImageUtils::TextureDesc` / `SamplerDesc` descriptions are available

#### Scenario: Compute pipeline facilities available from Rhi

- **WHEN** a client includes the compute kernel facility's header
- **THEN** a compute pipeline can be requested for a SPIR-V binary and dispatched with a resource dictionary, without any Asset dependency
- **AND** `Engine::Rhi::ComputeStage` and `Engine::Rhi::ComputeResourceBinding` are not part of the module

#### Scenario: RenderTargetTexture inherits from Rhi Texture

- **WHEN** `RenderTargetTexture` (in Render) is used
- **THEN** it derives from `Engine::Rhi::Texture` and the dependency direction is Render → Rhi

#### Scenario: Retirement facility available from Rhi

- **WHEN** a client includes the retirement facility's header
- **THEN** `Engine::Rhi::EpochTracker` is available under `Engine::Rhi`
- **AND** the epoch tracker is the only recipient of retired buffer allocations: the allocator and `BufferAllocation` name it directly, with no interface in between
- **AND** the facility does not depend on headers from `engine/Render/`

#### Scenario: Shared-ownership buffer factory available from Rhi

- **WHEN** a client needs a buffer whose lifetime must outlive the component that created it
- **THEN** no reference-counted buffer factory is available from `Rhi`
- **AND** the buffer is uniquely owned and referenced through a raw pointer, and the owner's lifetime is what keeps it alive

#### Scenario: Descriptor arena available from Rhi

- **WHEN** a client includes the descriptor arena's header
- **THEN** the arena is available under `Engine::Rhi`, keeping acquired descriptor sets resident and reusable across submission epochs
- **AND** the arena does not depend on headers from `engine/Render/`

## REMOVED Requirements

### Requirement: ComputeStage has no Asset dependency

**Reason**: `ComputeStage` is deleted, so a requirement constraining its construction — including the descriptor-pool clause the preceding `descriptor-arena-epoch-buckets` change added to it — has no subject. The properties it protected are not dropped: the replacement facility must equally avoid the Asset module, and that is stated as *A kernel has no Asset dependency* in the `rhi-compute-kernel` capability; and no consumer may own a descriptor pool, which is stated as *Descriptor sets come from a device-scoped arena* in the `rhi-descriptor-arena` capability.

**Migration**: Replace every `ComputeStage` construction and `Instantiate(std::vector<uint32_t>, name)` call with a kernel request for the same SPIR-V module and name; the new request path equally takes SPIR-V words and has no asset-typed overload. Verify by searching `engine/Rhi/` for `ComputeStage` and by checking that no kernel header includes an Asset header.
