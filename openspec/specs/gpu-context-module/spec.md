# GpuContext Module

## Purpose

Defines the `Rhi` shared library — the successor of `GpuContext` — as an independent shared library that owns both the `DeviceInterface` and the `AllocatorState`, decoupling GPU resource ownership from the Render module so that headless GPU compute workloads can run without any `RenderSystem`.

## Requirements

### Requirement: GpuContext exists as independent shared library

The engine SHALL provide a `Rhi` shared library (`engine/Rhi/`) — the successor of `GpuContext` (`engine/GpuContext/`) — that compiles independently from the Render module.

#### Scenario: Rhi DLL builds without Render dependencies

- **WHEN** `cmake --build --preset debug` is invoked
- **THEN** `EngineRhi.dll` is produced in the build output directory
- **AND** `Rhi` does not include any headers from `engine/Render/`

#### Scenario: Rhi links against Engine.dll

- **WHEN** `Engine.dll` is built
- **THEN** `target_link_libraries(Engine PUBLIC EngineRhi)` succeeds

### Requirement: AllocatorState depends on DeviceInterface, not RenderSystem

`AllocatorState` SHALL accept `DeviceInterface&` as its constructor parameter instead of `RenderSystem&`.

#### Scenario: AllocatorState::Create uses DeviceInterface

- **WHEN** `AllocatorState::Create()` is called
- **THEN** it retrieves `vk::Device`, `vk::PhysicalDevice`, and `vk::Instance` from the stored `DeviceInterface&`
- **AND** it creates a VMA allocator with those handles

#### Scenario: AllocatorState::AllocateBuffer debug naming

- **WHEN** `AllocateBuffer()` is called
- **THEN** it calls `m_device_interface.GetDevice()` to set the debug name on the created buffer

### Requirement: MemoryTypes and MemoryAllocation reside in Rhi

`MemoryTypes.h` and `MemoryAllocation.h/.cpp` SHALL reside in `engine/Rhi/` (moved from `engine/GpuContext/`, originally `engine/Render/Memory/`).

#### Scenario: BufferType and ImageMemoryType available from Rhi

- **WHEN** a client includes `Rhi/MemoryTypes.h`
- **THEN** `Engine::Rhi::BufferType`, `Engine::Rhi::ImageMemoryType`, and related bitflag enums are available
- **AND** the only dependency is `Core/flagbits.h`

#### Scenario: BufferAllocation and ImageAllocation available from Rhi

- **WHEN** a client includes `Rhi/MemoryAllocation.h`
- **THEN** `Engine::Rhi::BufferAllocation` and `Engine::Rhi::ImageAllocation` types are available

### Requirement: Rhi exports symbols via RHI_API macro

`Rhi` SHALL use a `RHI_API` export macro pattern matching `Core`'s `CORE_API` (renamed from `GPU_CONTEXT_API`).

#### Scenario: Export macro defined for DLL builds

- **WHEN** building `Rhi` as a shared library on Windows
- **THEN** `RHI_DLL_EXPORTS` is defined via `target_compile_definitions`
- **AND** `RHI_API` expands to `__declspec(dllexport)`

#### Scenario: Export macro defined for consumers

- **WHEN** linking against `Rhi` (e.g., from `Engine.dll`)
- **THEN** `RHI_DLL_EXPORTS` is NOT defined
- **AND** `RHI_API` expands to `__declspec(dllimport)`

### Requirement: GpuContext supports standalone headless usage

A program SHALL be able to create the Rhi device facilities without any `RenderSystem` and run GPU compute workloads.

#### Scenario: Standalone headless compute test

- **WHEN** a test program creates a `DeviceInterface` with `DeviceConfiguration{.window = nullptr}` and an `AllocatorState`
- **THEN** the Vulkan device is created successfully with a graphics queue
- **AND** `AllocatorState::AllocateBuffer` works for allocating GPU buffers
- **AND** a compute shader can be dispatched and results read back
