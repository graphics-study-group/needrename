## MODIFIED Requirements

### Requirement: Rhi hosts the generic GPU infrastructure types

`Rhi` SHALL contain the following types, moved from `engine/Render/` without semantic changes: `DeviceInterface`, `AllocatorState`, `MemoryTypes` / `MemoryAllocation`, `DeviceBuffer`, `ComputeBuffer`, `StructuredBuffer`, `StructuredBufferPlacer`, `Texture`, `ImageTexture`, `TextureSubresourceView`, `ImageUtils`, `ImmutableResourceCache`, `SubmissionHelper`, `ComputeStage`, `ComputeResourceBinding`, `ShaderResourceBinding`, `ShaderParameterLayout`, `ShaderInterface`, `MemoryAccessTypes`, `PipelineEnums`.

`Rhi` SHALL additionally own the device-scoped GPU resource retirement facility and its supporting types: the submission epoch tracker, which is itself the sole recipient of retired buffer allocations. It resides in `Rhi` because it depends on the device and allocator only, and both `Render` and `Physics` use it equally.

`Rhi` SHALL NOT provide a shared-ownership (reference-counted) buffer factory. A buffer whose lifetime must outlive the component that created it is uniquely owned by whoever owns that lifetime, and is referenced elsewhere through a raw pointer.

#### Scenario: Buffer types available from Rhi

- **WHEN** a client includes `Rhi/ComputeBuffer.h`
- **THEN** `Engine::Rhi::ComputeBuffer` and its `CreateUnique(allocator, ...)` factory are available

#### Scenario: Texture types available from Rhi

- **WHEN** a client includes `Rhi/Texture.h`
- **THEN** `Engine::Rhi::Texture` and the `Engine::Rhi::ImageUtils::TextureDesc` / `SamplerDesc` descriptions are available

#### Scenario: Compute pipeline facilities available from Rhi

- **WHEN** a client includes `Rhi/ComputeStage.h`
- **THEN** `Engine::Rhi::ComputeStage` / `Engine::Rhi::ComputeResourceBinding` are available, and a compute pipeline can be created from SPIR-V binary without any Asset dependency

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
