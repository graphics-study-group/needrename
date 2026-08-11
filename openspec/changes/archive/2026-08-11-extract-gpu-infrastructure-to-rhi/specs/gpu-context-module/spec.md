# GpuContext Module

## Purpose

Defines `GpuContext` as an independent shared library that owns both the `DeviceInterface` and the `AllocatorState`, decoupling GPU resource ownership from the Render module so that headless GPU compute workloads can run without any `RenderSystem`.

## MODIFIED Requirements

### Requirement: GpuContext exists as independent shared library

The engine SHALL provide a `Rhi` shared library (`engine/Rhi/`) — the successor of `GpuContext` (`engine/GpuContext/`) — that compiles independently from the Render module.

#### Scenario: Rhi DLL builds without Render dependencies

- **WHEN** `cmake --build --preset debug` is invoked
- **THEN** `Rhi.dll` is produced in the build output directory
- **AND** `Rhi` does not include any headers from `engine/Render/`

#### Scenario: Rhi links against engine.dll

- **WHEN** `engine.dll` is built
- **THEN** `target_link_libraries(engine PUBLIC Rhi)` succeeds

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

- **WHEN** linking against `Rhi` (e.g., from `engine.dll`)
- **THEN** `RHI_DLL_EXPORTS` is NOT defined
- **AND** `RHI_API` expands to `__declspec(dllimport)`

### Requirement: GpuContext supports standalone headless usage

A program SHALL be able to create the Rhi device facilities without any `RenderSystem` and run GPU compute workloads.

#### Scenario: Standalone headless compute test

- **WHEN** a test program creates a `DeviceInterface` with `DeviceConfiguration{.window = nullptr}` and an `AllocatorState`
- **THEN** the Vulkan device is created successfully with a graphics queue
- **AND** `AllocatorState::AllocateBuffer` works for allocating GPU buffers
- **AND** a compute shader can be dispatched and results read back

## REMOVED Requirements

### Requirement: GpuContext aggregates DeviceInterface and AllocatorState

**Reason**: The `GpuContext` aggregator class is unused dead code (RenderSystem constructs `DeviceInterface` and `AllocatorState` directly). Deleted as part of the Rhi migration; composition stays explicit at call sites.

**Migration**: Construct `DeviceInterface` and `AllocatorState` directly. See `rhi-module` capability for the moved facilities and the unified `Engine::Rhi` namespace.
