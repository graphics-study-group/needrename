## ADDED Requirements

### Requirement: IPresentProvider interface defines frame acquisition and presentation
The system SHALL provide an `IPresentProvider` interface that abstracts how frames are acquired and presented.

#### Scenario: Interface methods
- **WHEN** `IPresentProvider` is defined in `Render/RenderSystem/IPresentProvider.h`
- **THEN** it SHALL declare the following pure virtual methods:
  - `GetExtent() const` → `vk::Extent2D`
  - `GetColorFormat() const` → `vk::Format`
  - `GetImageCount() const` → `uint32_t`
  - `AcquireNextImage(vk::Device, vk::Semaphore, uint64_t)` → `uint32_t`
  - `CompleteFrame(vk::Device, const RenderTargetTexture&, uint32_t image_index, const FrameSyncInfo&)` → `bool`
  - `Recreate(vk::Extent2D)`

#### Scenario: FrameSyncInfo is a pre-built sync struct
- **WHEN** `FrameSyncInfo` is defined alongside `IPresentProvider`
- **THEN** it SHALL contain:
  - `std::array<vk::SemaphoreSubmitInfo, 2> wait` (timeline + image_acquired)
  - `std::array<vk::SemaphoreSubmitInfo, 2> signal` (timeline + copy_completed)
  - `vk::Fence fence`
- **AND** the provider SHALL pass these directly into `vk::SubmitInfo2` without interpreting their semantics

### Requirement: SwapchainPresentProvider implements windowed presentation
`SwapchainPresentProvider` SHALL implement `IPresentProvider` for windowed mode with a real Vulkan swapchain.

#### Scenario: AcquireNextImage uses vkAcquireNextImageKHR
- **WHEN** `AcquireNextImage` is called
- **THEN** it calls `vkAcquireNextImageKHR` with the internal swapchain handle
- **AND** returns the acquired image index

#### Scenario: SwapchainPresentProvider owns copy command buffers
- **WHEN** `SwapchainPresentProvider::Initialize` is called
- **THEN** it allocates `copy_to_swapchain_command_buffers` from the graphics command pool
- **AND** the command buffers are reset and reused each frame inside `CompleteFrame`

#### Scenario: CompleteFrame blits, submits and presents
- **WHEN** `CompleteFrame` is called with a source RTT, image index, and `FrameSyncInfo`
- **THEN** it records a blit command (with pre/post copy barriers) from the RTT to the swapchain image at the given index into its own copy command buffer
- **AND** submits the command buffer via `vkQueueSubmit2` using `sync.wait` / `sync.signal` arrays and `sync.fence`
- **AND** calls `vkQueuePresentKHR` waiting on the copy_completed semaphore
- **AND** returns `true` if the swapchain needs recreation (`VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR`), otherwise `false`

#### Scenario: GetExtent returns swapchain extent
- **WHEN** `GetExtent` is called
- **THEN** it returns the current swapchain extent

#### Scenario: GetColorFormat returns surface format
- **WHEN** `GetColorFormat` is called
- **THEN** it returns the swapchain surface format

#### Scenario: Recreate rebuilds the swapchain
- **WHEN** `Recreate` is called with a new extent
- **THEN** it destroys the old swapchain and creates a new one with the given extent
- **AND** re-allocates or re-fetches swapchain images and copy command buffers as needed

### Requirement: HeadlessPresentProvider implements no-op presentation
`HeadlessPresentProvider` SHALL implement `IPresentProvider` for headless mode without any swapchain or present.

#### Scenario: AcquireNextImage returns synthetic index
- **WHEN** `AcquireNextImage` is called
- **THEN** it returns `(counter++) % GetImageCount()` without any Vulkan calls

#### Scenario: CompleteFrame is a no-op
- **WHEN** `CompleteFrame` is called with any parameters
- **THEN** it immediately returns `false` without any Vulkan operations

#### Scenario: GetExtent returns configured resolution
- **WHEN** `GetExtent` is called
- **THEN** it returns the extent provided at construction time

#### Scenario: GetImageCount returns FRAMES_IN_FLIGHT
- **WHEN** `GetImageCount` is called
- **THEN** it returns `FRAMES_IN_FLIGHT` (3)

#### Scenario: Recreate updates extent
- **WHEN** `Recreate` is called with a new extent
- **THEN** it stores the new extent for future `GetExtent` calls

### Requirement: RenderSystem owns IPresentProvider instead of Swapchain
`RenderSystem` SHALL own an `IPresentProvider` via `std::unique_ptr` instead of a `Swapchain` value member.

#### Scenario: RenderSystem::Create selects provider type
- **WHEN** `RenderSystem::Create` is called with a valid `SDLWindow`
- **THEN** it creates a `SwapchainPresentProvider` and stores it in `m_present_provider`
- **WHEN** `RenderSystem::Create` is called with a null window (headless)
- **THEN** it creates a `HeadlessPresentProvider` with the configured resolution

#### Scenario: RenderSystem exposes present provider
- **WHEN** `GetPresentProvider()` is called
- **THEN** it returns a reference to the owned `IPresentProvider`

#### Scenario: GetSwapchain() is removed
- **WHEN** code attempts to call `RenderSystem::GetSwapchain()`
- **THEN** compilation fails (method no longer exists)

### Requirement: FrameManager uses IPresentProvider for all present operations
`FrameManager` SHALL interact with `IPresentProvider` for frame acquisition and presentation, never calling `Swapchain` methods directly. All synchronization primitives (timeline semaphores, binary semaphores, fences, frame-in-flight counter) remain owned by `FrameManager`.

#### Scenario: FrameManager::Create receives IPresentProvider
- **WHEN** `FrameManager::Create` is called
- **THEN** it receives an `IPresentProvider&` parameter
- **AND** stores a pointer to it for subsequent frame operations

#### Scenario: copy-to-swapchain semaphores sized by provider image count
- **WHEN** `FrameManager::Create` initializes semaphores
- **THEN** it calls `m_present_provider->GetImageCount()` instead of `m_system.GetSwapchain().GetFrameCount()`

#### Scenario: StartFrame delegates acquisition to provider
- **WHEN** `StartFrame` is called
- **THEN** it calls `m_present_provider->AcquireNextImage(device, semaphore, timeout)` to get the next framebuffer index
- **AND** stores that index as `current_framebuffer`

#### Scenario: PresentToFramebuffer builds FrameSyncInfo and delegates
- **WHEN** `PresentToFramebuffer` is called
- **THEN** it builds a `FrameSyncInfo` from its internal timeline, image_acquired, and copy_completed semaphores plus the command_executed fence
- **AND** calls `m_present_provider->CompleteFrame(device, src, image_index, sync)`
- **AND** calls `pimpl->CompleteFrame()` afterwards for readback processing, frame-in-flight advance, and timeline `EndFrame()`
- **AND** returns the provider's `needs_recreating` result

#### Scenario: PresentToFramebuffer no longer touches Swapchain directly
- **WHEN** `PresentToFramebuffer` executes
- **THEN** it does NOT call `m_system.GetSwapchain()` at any point
- **AND** the copy command buffer recording, submit, and present are entirely inside the provider

### Requirement: Extent and color format consumers use IPresentProvider
All engine code that previously called `RenderSystem::GetSwapchain().GetExtent()` or `GetSwapchain().GetColorFormat()` SHALL obtain these values from `IPresentProvider` instead.

#### Scenario: CommandBuffer::DrawRenderers reads extent from provider
- **WHEN** the two-parameter `DrawRenderers` overload is called
- **THEN** it calls `m_system.GetPresentProvider().GetExtent()` instead of `m_system.GetSwapchain().GetExtent()`

#### Scenario: ComplexRenderGraphBuilder accepts explicit extent
- **WHEN** `BuildDefaultRenderGraph` is called
- **THEN** it uses the `texture_width` and `texture_height` parameters directly (already the case)

#### Scenario: GUISystem reads color format from provider
- **WHEN** `GUISystem` needs the color attachment format
- **THEN** it calls `render_system.GetPresentProvider().GetColorFormat()` instead of `render_system.GetSwapchain().GetColorFormat()`

### Requirement: Headless mode skips window-dependent subsystems
In headless mode, `MainClass::Initialize` SHALL not create `SDLWindow`, `GUISystem`, or `Input`.

#### Scenario: Headless Initialize
- **WHEN** `Initialize` is called with `StartupOptions::headless == true`
- **THEN** `SDLWindow` is not created (`this->window` remains `nullptr`)
- **AND** `RenderSystem` is constructed with an empty `std::weak_ptr<SDLWindow>`
- **AND** `GUISystem` and `Input` are not created
- **AND** `RenderSystem::Create()` produces a headless `HeadlessPresentProvider`

#### Scenario: Headless RenderSystem::CompleteFrame skips present
- **WHEN** `CompleteFrame` is called in headless mode
- **THEN** `HeadlessPresentProvider::CompleteFrame` is invoked (returns `false`, no-op)
- **AND** no `vkQueuePresentKHR` is called
