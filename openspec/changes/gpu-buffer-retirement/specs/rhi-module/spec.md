# rhi-module

## MODIFIED Requirements

### Requirement: Rhi hosts the generic GPU infrastructure types

`Rhi` SHALL contain the following types, moved from `engine/Render/` without semantic changes: `DeviceInterface`, `AllocatorState`, `MemoryTypes` / `MemoryAllocation`, `DeviceBuffer`, `ComputeBuffer`, `StructuredBuffer`, `StructuredBufferPlacer`, `Texture`, `ImageTexture`, `TextureSubresourceView`, `ImageUtils`, `ImmutableResourceCache`, `SubmissionHelper`, `ComputeStage`, `ComputeResourceBinding`, `ShaderResourceBinding`, `ShaderParameterLayout`, `ShaderInterface`, `MemoryAccessTypes`, `PipelineEnums`.

`Rhi` SHALL additionally own the device-scoped GPU resource retirement facility and its supporting types: the submission epoch tracker, the allocation retire sink that buffer allocations report to, and the shared-ownership buffer factory. These reside in `Rhi` because they depend on the device and allocator only, and both `Render` and `Physics` use them equally.

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

### Requirement: Rhi supports standalone headless usage

A program SHALL be able to create the Rhi device facilities without any `RenderSystem` and run GPU compute workloads.

#### Scenario: Standalone headless compute test

- **WHEN** a test program creates a `DeviceInterface` with `DeviceConfiguration{.window = nullptr}` and an `AllocatorState`
- **THEN** the Vulkan device is created successfully with a graphics queue
- **AND** `AllocatorState::AllocateBuffer` works for allocating GPU buffers
- **AND** a compute shader can be dispatched and results read back

#### Scenario: Standalone construction without a DeviceContext

- **WHEN** a standalone program creates a `DeviceInterface` and an `AllocatorState` directly, as the standalone Rhi tests do
- **THEN** the retirement facility can be constructed from those same facilities and installed on the allocator
- **AND** when it is not installed, buffer destruction behaves exactly as before (immediate free), so existing standalone setups keep their semantics

#### Scenario: Headless retirement without a frame loop

- **WHEN** a headless test installs the retirement facility, then resizes and destroys buffers between blocking submissions, with no presentation frame loop and no `FrameManager`
- **THEN** no allocation is freed while its submission is outstanding
- **AND** all retired allocations are released once the device reports idle
