# rhi-module

## MODIFIED Requirements

### Requirement: Rhi hosts the generic GPU infrastructure types

`Rhi` SHALL contain the following types, moved from `engine/Render/` without semantic changes: `DeviceInterface`, `AllocatorState`, `MemoryTypes` / `MemoryAllocation`, `DeviceBuffer`, `ComputeBuffer`, `StructuredBuffer`, `StructuredBufferPlacer`, `Texture`, `ImageTexture`, `TextureSubresourceView`, `ImageUtils`, `ImmutableResourceCache`, `SubmissionHelper`, `ComputeStage`, `ComputeResourceBinding`, `ShaderResourceBinding`, `ShaderParameterLayout`, `ShaderInterface`, `MemoryAccessTypes`, `PipelineEnums`.

`Rhi` SHALL additionally own the device-scoped GPU resource retirement facility and its supporting types: the submission epoch tracker, the allocation retire sink that buffer allocations report to, and the shared-ownership buffer factory. These reside in `Rhi` because they depend on the device and allocator only, and both `Render` and `Physics` use them equally.

`Rhi` SHALL additionally own the device-scoped descriptor arena: the single owner of descriptor pools, which keeps acquired sets resident and reusable across submission epochs and reclaims them under one of two triggers — cache pressure for per-dispatch compute bindings and an explicit owner release for long-lived material, scene and camera state. The arena resides in `Rhi` because both `Render` (materials, scene data, cameras) and compute consumers use it equally, and neither may own a pool.

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
- **THEN** the epoch tracker and the allocation retire sink are available under `Engine::Rhi`
- **AND** neither depends on headers from `engine/Render/`

#### Scenario: Shared-ownership buffer factory available from Rhi

- **WHEN** a client needs a buffer whose lifetime must outlive the component that created it
- **THEN** a shared-ownership factory is available alongside the unique-ownership factory, returning a reference-counted handle to the same buffer type

#### Scenario: Descriptor arena available from Rhi

- **WHEN** a client includes the descriptor arena's header
- **THEN** the arena and its two acquisition lifetimes are available under `Engine::Rhi`
- **AND** the arena does not depend on headers from `engine/Render/`

### Requirement: ComputeStage has no Asset dependency

`ComputeStage` SHALL be constructible from Rhi facilities only and SHALL NOT depend on the Asset module. It SHALL provide `Instantiate(const std::vector<uint32_t>& code, std::string_view name)`; the `Instantiate(ShaderAsset&)` overload and the `IInstantiatedFromAsset` inheritance SHALL be removed.

`ComputeStage` SHALL own its pipeline, pipeline layout and descriptor set layout, and SHALL NOT create or own a descriptor pool: descriptor sets used with the stage come from the device descriptor arena.

#### Scenario: ComputeStage constructed from Rhi facilities

- **WHEN** `ComputeStage` is constructed with `(const DeviceInterface&, const AllocatorState&)` and instantiated from SPIR-V binary
- **THEN** a compute pipeline, pipeline layout and descriptor set layout are created on the device
- **AND** no descriptor pool is created for the stage

#### Scenario: Stage exposes no pool

- **WHEN** a caller needs the descriptor set of a compute binding
- **THEN** the set is acquired from the device descriptor arena
- **AND** the stage offers no descriptor pool for the caller to allocate from

#### Scenario: Asset-path caller adapted

- **WHEN** `ComplexRenderGraphBuilder` creates its bloom compute stage
- **THEN** it passes the shader's SPIR-V binary and name directly, with no `ShaderAsset` overload used
