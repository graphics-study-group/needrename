# GpuContext Module

## Purpose

Defines `GpuContext` as an independent shared library that owns both the `DeviceInterface` and the `AllocatorState`, decoupling GPU resource ownership from the Render module so that headless GPU compute workloads can run without any `RenderSystem`.

## Requirements

### Requirement: GpuContext exists as independent shared library

The engine SHALL provide a `GpuContext` shared library (`engine/GpuContext/`) that compiles independently from the Render module.

#### Scenario: GpuContext DLL builds without Render dependencies

- **WHEN** `cmake --build --preset debug` is invoked
- **THEN** `GpuContext.dll` is produced in the build output directory
- **AND** `GpuContext` does not include any headers from `engine/Render/`

#### Scenario: GpuContext links against engine.dll

- **WHEN** `engine.dll` is built
- **THEN** `target_link_libraries(engine PUBLIC GpuContext)` succeeds

### Requirement: GpuContext aggregates DeviceInterface and AllocatorState

The `GpuContext` class SHALL own both a `DeviceInterface` and an `AllocatorState`, and expose accessors for both.

#### Scenario: GpuContext construction

- **WHEN** `GpuContext` is constructed with a `DeviceConfiguration`
- **THEN** it creates a `DeviceInterface` with that configuration
- **AND** it creates an `AllocatorState` referencing that `DeviceInterface`
- **AND** it calls `AllocatorState::Create()` to initialize the VMA allocator

#### Scenario: GpuContext accessor methods

- **WHEN** `GetDevice()` is called
- **THEN** it returns the `vk::Device` from the owned `DeviceInterface`
- **WHEN** `GetAllocatorState()` is called
- **THEN** it returns a `const AllocatorState&` reference
- **WHEN** `GetDeviceInterface()` is called
- **THEN** it returns a `const DeviceInterface&` reference

### Requirement: AllocatorState depends on DeviceInterface, not RenderSystem

`AllocatorState` SHALL accept `DeviceInterface&` as its constructor parameter instead of `RenderSystem&`.

#### Scenario: AllocatorState::Create uses DeviceInterface

- **WHEN** `AllocatorState::Create()` is called
- **THEN** it retrieves `vk::Device`, `vk::PhysicalDevice`, and `vk::Instance` from the stored `DeviceInterface&`
- **AND** it creates a VMA allocator with those handles

#### Scenario: AllocatorState::AllocateBuffer debug naming

- **WHEN** `AllocateBuffer()` is called
- **THEN** it calls `m_device_interface.GetDevice()` to set the debug name on the created buffer

### Requirement: MemoryTypes and MemoryAllocation reside in GpuContext

`MemoryTypes.h` and `MemoryAllocation.h/.cpp` SHALL be moved from `engine/Render/Memory/` to `engine/GpuContext/`.

#### Scenario: BufferType and ImageMemoryType available from GpuContext

- **WHEN** a client includes `GpuContext/MemoryTypes.h`
- **THEN** `Engine::BufferType`, `Engine::ImageMemoryType`, and related bitflag enums are available
- **AND** the only dependency is `Core/flagbits.h`

#### Scenario: BufferAllocation and ImageAllocation available from GpuContext

- **WHEN** a client includes `GpuContext/MemoryAllocation.h`
- **THEN** `Engine::BufferAllocation` and `Engine::ImageAllocation` types are available

### Requirement: GpuContext exports symbols via GPU_CONTEXT_API macro

`GpuContext` SHALL use a `GPU_CONTEXT_API` export macro pattern matching `Core`'s `CORE_API`.

#### Scenario: Export macro defined for DLL builds

- **WHEN** building `GpuContext` as a shared library on Windows
- **THEN** `GPU_CONTEXT_DLL_EXPORTS` is defined via `target_compile_definitions`
- **AND** `GPU_CONTEXT_API` expands to `__declspec(dllexport)`

#### Scenario: Export macro defined for consumers

- **WHEN** linking against `GpuContext` (e.g., from `engine.dll`)
- **THEN** `GPU_CONTEXT_DLL_EXPORTS` is NOT defined
- **AND** `GPU_CONTEXT_API` expands to `__declspec(dllimport)`

### Requirement: GpuContext supports standalone headless usage

A program SHALL be able to create a `GpuContext` without any `RenderSystem` and run GPU compute workloads.

#### Scenario: Standalone headless compute test

- **WHEN** a test program creates `GpuContext` with `DeviceConfiguration{.window = nullptr}`
- **THEN** the Vulkan device is created successfully with a graphics queue
- **AND** `AllocatorState::AllocateBuffer` works for allocating GPU buffers
- **AND** a compute shader can be dispatched and results read back
