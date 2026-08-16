# Headless Vulkan Device

## Purpose

Defines `DeviceInterface`'s headless creation path: a Vulkan instance, physical device and logical device can be created without an `SDL_Window` or surface, omitting surface extensions and present queue support while keeping all surface-independent features identical.

## Requirements

### Requirement: DeviceInterface supports headless Vulkan device creation

`DeviceInterface` SHALL accept a `nullptr` `SDL_Window*` in its `DeviceConfiguration` to create a Vulkan device without a surface.

#### Scenario: Headless construction skips surface creation

- **WHEN** `DeviceInterface` is constructed with `DeviceConfiguration{.window = nullptr}`
- **THEN** `CreateSurface()` is not called
- **AND** no `vk::SurfaceKHR` is created
- **AND** the constructor completes successfully

#### Scenario: Headless instance creation omits surface extensions

- **WHEN** the Vulkan instance is created in headless mode
- **THEN** `VK_KHR_surface` and platform-specific surface extensions are NOT added to the instance extension list
- **AND** `SDL_Vulkan_GetInstanceExtensions` is NOT called
- **AND** Vulkan validation layer debug extensions are still added (if applicable)

#### Scenario: Windowed instance creation includes surface extensions (unchanged)

- **WHEN** `DeviceInterface` is constructed with a valid `SDL_Window*`
- **THEN** `VK_KHR_surface` and platform-specific surface extensions ARE added
- **AND** behavior is identical to the pre-refactor implementation

### Requirement: Headless physical device selection does not require present support

When selecting a physical device in headless mode, `DeviceInterface` SHALL not require present queue support.

#### Scenario: Headless physical device scoring

- **WHEN** `GetPhysicalDevice` evaluates candidates in headless mode
- **THEN** it SHALL NOT check `SwapchainSupport` (no surface to query against)
- **AND** it SHALL NOT check `GetQueueFamily(QueueFamilyType::GraphicsPresent)` availability
- **AND** it SHALL still require graphics, compute, and transfer queue support
- **AND** it SHALL still require `dynamicRendering`, `synchronization2`, `timelineSemaphore` features

#### Scenario: Windowed physical device selection (unchanged)

- **WHEN** `GetPhysicalDevice` evaluates candidates in windowed mode
- **THEN** it SHALL still check swapchain support and present queue availability
- **AND** behavior is identical to the pre-refactor implementation

### Requirement: Headless device creation omits present queue

When creating the logical device and command pools in headless mode, `DeviceInterface` SHALL not create a present queue or present command pool.

#### Scenario: Headless queue creation

- **WHEN** logical device queues are created in headless mode
- **THEN** graphics queue family is still selected (hardware capability, independent of surface)
- **AND** `QueueInfo::presentQueue` is set to `VK_NULL_HANDLE` (or equivalent null state)
- **AND** `QueueInfo::presentPool` is not created

#### Scenario: Headless command pool creation

- **WHEN** command pools are created in headless mode
- **THEN** graphics command pool is created normally
- **AND** present command pool is not created

#### Scenario: Graphics queue is always available in headless mode

- **WHEN** a headless `DeviceInterface` is successfully created
- **THEN** `GetQueueInfo().graphicsQueue` returns a valid Vulkan queue handle
- **AND** `GetQueueInfo().graphicsPool` returns a valid command pool handle
- **AND** the queue can be used for compute dispatches, transfer commands, and offscreen graphics rendering

### Requirement: SDL_Vulkan_LoadLibrary works without a window

In headless mode, `DeviceInterface` SHALL call `SDL_Vulkan_LoadLibrary(nullptr)` to load the Vulkan loader without requiring an `SDL_Window`.

#### Scenario: Vulkan library loaded in headless mode

- **WHEN** `CreateInstance` is called in headless mode
- **THEN** `SDL_Vulkan_LoadLibrary(nullptr)` is called successfully
- **AND** the Vulkan function pointers are loaded for instance creation

### Requirement: All existing DeviceInterface features work in headless mode

All Vulkan features unrelated to surface/swapchain/present SHALL function identically regardless of headless or windowed mode.

#### Scenario: GPU memory allocation works in headless

- **WHEN** `AllocatorState::AllocateBuffer` is called using a headless `DeviceInterface`
- **THEN** the buffer is created successfully using VMA with the headless device

#### Scenario: Compute pipeline creation works in headless

- **WHEN** a compute pipeline is created using the headless `DeviceInterface`'s `vk::Device`
- **THEN** the pipeline is created successfully using the graphics queue
- **AND** compute dispatches execute correctly

#### Scenario: Queue submission works in headless

- **WHEN** a command buffer is submitted to the graphics queue in headless mode
- **THEN** the submission succeeds
- **AND** `vkQueueWaitIdle` functions correctly
