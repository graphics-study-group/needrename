# Rhi Module

## Purpose

Defines `Rhi` as the engine's generic GPU infrastructure layer: an independent shared library owning device, allocator, buffers, textures, immutable resource cache, the upload/submission queue, and compute pipeline facilities — all under the unified `Engine::Rhi` namespace. Render and Physics depend on it equally; it contains no rendering algorithm.

## Requirements

### Requirement: Rhi exists as independent shared library

The engine SHALL provide a `Rhi` shared library (`engine/Rhi/`) that compiles independently from the Render module. It SHALL NOT include any headers from `engine/Render/`.

#### Scenario: Rhi DLL builds without Render dependencies

- **WHEN** `cmake --build --preset debug` is invoked
- **THEN** `EngineRhi.dll` is produced in the build output directory
- **AND** `Rhi` does not include any headers from `engine/Render/`

#### Scenario: Rhi links against Engine.dll

- **WHEN** `Engine.dll` is built
- **THEN** `target_link_libraries(Engine PUBLIC EngineRhi)` succeeds

### Requirement: Rhi hosts the generic GPU infrastructure types

`Rhi` SHALL contain the following types, moved from `engine/Render/` without semantic changes: `DeviceInterface`, `AllocatorState`, `MemoryTypes` / `MemoryAllocation`, `DeviceBuffer`, `ComputeBuffer`, `StructuredBuffer`, `StructuredBufferPlacer`, `Texture`, `ImageTexture`, `TextureSubresourceView`, `ImageUtils`, `ImmutableResourceCache`, `SubmissionHelper`, `ComputeStage`, `ComputeResourceBinding`, `ShaderResourceBinding`, `ShaderParameterLayout`, `ShaderInterface`, `MemoryAccessTypes`, `PipelineEnums`.

`Rhi` SHALL additionally own the device-scoped GPU resource retirement facility and its supporting types: the submission epoch tracker — which is itself the sole recipient of retired buffer allocations — and the shared-ownership buffer factory. These reside in `Rhi` because they depend on the device and allocator only, and both `Render` and `Physics` use them equally.

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
- **THEN** a shared-ownership factory is available alongside the unique-ownership factory, returning a reference-counted handle to the same buffer type

### Requirement: Unified Engine::Rhi namespace

All types residing in `Rhi` SHALL use the namespace `Engine::Rhi`. The former namespaces `Engine::RenderSystemState`, `Engine::ImageUtils`, `Engine::PipelineUtils`, `Engine::ShdrRfl`, and bare `Engine` (for moved types) SHALL NOT be used for types residing in `Rhi`.

#### Scenario: Namespace-qualified access

- **WHEN** a client references a moved type
- **THEN** it is reachable as `Engine::Rhi::<TypeName>` (e.g. `Engine::Rhi::DeviceInterface`, `Engine::Rhi::SubmissionHelper`, `Engine::Rhi::ComputeBuffer`, `Engine::Rhi::ImageFormat`, `Engine::Rhi::FillingMode`)

#### Scenario: Serialized asset type names updated

- **WHEN** existing JSON asset files reference moved types by namespace-qualified name
- **THEN** they are rewritten to the `Engine::Rhi` names by the batch script, and assets load successfully

### Requirement: AllocatorState depends on DeviceInterface, not RenderSystem

`AllocatorState` SHALL accept `DeviceInterface&` as its constructor parameter instead of `RenderSystem&`.

#### Scenario: AllocatorState::Create uses DeviceInterface

- **WHEN** `AllocatorState::Create()` is called
- **THEN** it retrieves `vk::Device`, `vk::PhysicalDevice`, and `vk::Instance` from the stored `DeviceInterface&`
- **AND** it creates a VMA allocator with those handles

#### Scenario: AllocatorState::AllocateBuffer debug naming

- **WHEN** `AllocateBuffer()` is called
- **THEN** it calls `m_device_interface.GetDevice()` to set the debug name on the created buffer

### Requirement: ComputeStage has no Asset dependency

`ComputeStage` SHALL be constructible from Rhi facilities only and SHALL NOT depend on the Asset module. It SHALL provide `Instantiate(const std::vector<uint32_t>& code, std::string_view name)`; the `Instantiate(ShaderAsset&)` overload and the `IInstantiatedFromAsset` inheritance SHALL be removed.

#### Scenario: ComputeStage constructed from Rhi facilities

- **WHEN** `ComputeStage` is constructed with `(const DeviceInterface&, const AllocatorState&)` and instantiated from SPIR-V binary
- **THEN** a compute pipeline, pipeline layout, descriptor set layout, and descriptor pool are created on the device

#### Scenario: Asset-path caller adapted

- **WHEN** `ComplexRenderGraphBuilder` creates its bloom compute stage
- **THEN** it passes the shader's SPIR-V binary and name directly, with no `ShaderAsset` overload used

### Requirement: Rhi exports symbols via RHI_API macro

`Rhi` SHALL use a `RHI_API` export macro pattern matching `Core`'s `CORE_API`.

#### Scenario: Export macro defined for DLL builds

- **WHEN** building `Rhi` as a shared library on Windows
- **THEN** `RHI_DLL_EXPORTS` is defined via `target_compile_definitions`
- **AND** `RHI_API` expands to `__declspec(dllexport)`

#### Scenario: Export macro defined for consumers

- **WHEN** linking against `Rhi` (e.g., from `Engine.dll`)
- **THEN** `RHI_DLL_EXPORTS` is NOT defined
- **AND** `RHI_API` expands to `__declspec(dllimport)`

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
