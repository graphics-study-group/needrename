## ADDED Requirements

### Requirement: IPresentProvider interface defines frame acquisition, copy recording and presentation
The system SHALL provide an `IPresentProvider` interface that abstracts how frames are acquired, copied and presented. The interface SHALL NOT carry frame-lifecycle synchronization state.

#### Scenario: Interface methods
- **WHEN** `IPresentProvider` is defined in `Render/RenderSystem/IPresentProvider.h`
- **THEN** it SHALL declare the following pure virtual methods:
  - `GetExtent() const` → `vk::Extent2D`
  - `GetColorFormat() const` → `vk::Format`
  - `GetImageCount() const` → `uint32_t`
  - `AcquireNextImage(vk::Device, vk::Semaphore image_ready_semaphore, uint64_t timeout)` → `uint32_t`
  - `PrepareCopy(vk::Device, const RenderTargetTexture&, uint32_t image_index, MemoryAccessTypeImageBits last_access)` → `vk::CommandBuffer`
  - `Present(vk::Device, uint32_t image_index, vk::Semaphore frame_done_semaphore)` → `bool`
  - `Recreate(vk::Extent2D)`

#### Scenario: Interface carries no sync-object arrays
- **WHEN** `IPresentProvider` is defined
- **THEN** the interface exposes no wait/signal arrays and no fence parameters
- **AND** the only synchronization object crossing the interface is the frame completion credential (`vk::Semaphore` + `uint64_t` value) passed into `Present`

#### Scenario: Present waits on the frame completion credential
- **WHEN** `Present` is called
- **THEN** it waits on the provided credential (the frame-completed binary semaphore, `frame_completed_semaphores[fif]`) before presenting
- **AND** the credential is a binary semaphore signaled by the frame-completion batch (binary, because `vkQueuePresentKHR` accepts only binary semaphores)
- **AND** the credential corresponds to the frame in which the presented image was copied (copy completion is part of frame completion)

### Requirement: AcquireNextImage contract — MUST signal image_ready_semaphore
`AcquireNextImage` SHALL return the next presentable image index and MUST signal `image_ready_semaphore` when that image becomes available for writing.

#### Scenario: Windowed acquire signals via vkAcquireNextImageKHR
- **WHEN** `SwapchainPresentProvider::AcquireNextImage` is called
- **THEN** it calls `vkAcquireNextImageKHR(swapchain, timeout, image_ready_semaphore, VK_NULL_HANDLE)`
- **AND** returns the acquired image index immediately (CPU never blocks on acquire)

#### Scenario: Headless acquire signals via no-op empty submit
- **WHEN** `HeadlessPresentProvider::AcquireNextImage` is called
- **THEN** it returns `(counter++) % GetImageCount()`
- **AND** signals `image_ready_semaphore` via an empty `vkQueueSubmit2` so the frame-completion batch gate is satisfied

#### Scenario: Out-of-date returns sentinel
- **WHEN** acquire returns `VK_ERROR_OUT_OF_DATE_KHR`
- **THEN** `AcquireNextImage` returns `~0u`
- **AND** `RenderSystem::StartFrame` (not `FrameManager::StartFrame`) detects the sentinel, recreates the swapchain (`UpdateSwapchain`) and retries acquisition once
- **AND** on failure `FrameManager::StartFrame` leaves the frame state untouched — `current_framebuffer` keeps its previous value, the fence is NOT reset, the frame-in-flight counter is NOT advanced — so the caller can safely skip the frame or retry without deadlocking
- **AND** the sentinel is never used as a framebuffer index (`m_copy_command_buffers[~0u]` is out of bounds)

### Requirement: SwapchainPresentProvider implements windowed copy recording and presentation
`SwapchainPresentProvider` SHALL implement `IPresentProvider` for windowed mode with a real Vulkan swapchain.

#### Scenario: SwapchainPresentProvider owns copy command buffers
- **WHEN** `SwapchainPresentProvider::Initialize` is called
- **THEN** it allocates one copy command buffer per swapchain image from the graphics command pool

#### Scenario: PrepareCopy records blit with last_access-derived barrier
- **WHEN** `PrepareCopy` is called with a source RTT, image index, and `last_access`
- **THEN** it resets the copy command buffer for that image index (safe: the image is only re-acquired after its previous present)
- **AND** records a blit with pre/post barriers whose source layout is derived from `last_access` via `GetImageLayout`
- **AND** returns the recorded command buffer; it does NOT submit

#### Scenario: Present waits on credential and reports recreation
- **WHEN** `Present` is called with the frame completion credential
- **THEN** it calls `vkQueuePresentKHR` on the present queue waiting on the credential
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

### Requirement: HeadlessPresentProvider implements no-op copy and presentation
`HeadlessPresentProvider` SHALL implement `IPresentProvider` for headless mode without any swapchain or present.

#### Scenario: AcquireNextImage returns synthetic index
- **WHEN** `HeadlessPresentProvider::AcquireNextImage` is called
- **THEN** it returns `(counter++) % GetImageCount()` and signals `image_ready_semaphore` via an empty submit (see acquire contract)

#### Scenario: PrepareCopy returns nullptr
- **WHEN** `HeadlessPresentProvider::PrepareCopy` is called with any parameters
- **THEN** it returns `nullptr` (the frame-completion batch carries no copy CB)

#### Scenario: Present is a no-op
- **WHEN** `HeadlessPresentProvider::Present` is called with any parameters
- **THEN** it returns `false` without any Vulkan presentation operations

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

#### Scenario: CompleteFrame parameters are simplified
- **WHEN** `RenderSystem::CompleteFrame` is called
- **THEN** it accepts only `(const RenderTargetTexture&, MemoryAccessTypeImageBits last_access)` — the `width`/`height`/`offset_x`/`offset_y` parameters are removed (the blit region derives from the provider extent)

### Requirement: FrameManager performs the frame-completion batch
`FrameManager` SHALL execute the single per-frame frame-completion submit itself. It interacts with `IPresentProvider` for acquisition, copy recording and presentation only. All synchronization primitives (timeline semaphores, acquire semaphores, fences, frame-in-flight counter) remain owned by `FrameManager`.

#### Scenario: FrameManager::Create receives IPresentProvider
- **WHEN** `FrameManager::Create` is called
- **THEN** it receives an `IPresentProvider&` parameter
- **AND** stores a pointer to it for subsequent frame operations

#### Scenario: StartFrame delegates acquisition to provider
- **WHEN** `StartFrame` is called
- **THEN** it calls `m_present_provider->AcquireNextImage(device, image_acquired_semaphores[fif], timeout)`
- **AND** stores the returned index as `current_framebuffer`
- **AND** when the provider returns `~0u`, it recreates the swapchain and retries acquisition (never uses `~0u` as a framebuffer index)

#### Scenario: SubmitFrame records copy and submits one batch
- **WHEN** `SubmitFrame` is called with the present texture and `last_access`
- **THEN** it ends the main command buffer
- **AND** calls `m_present_provider->PrepareCopy(device, present_texture, framebuffer, last_access)`
- **AND** submits ONE `vkQueueSubmit2` batch containing the main render CB and the copy CB (when non-null), with:
  - waits: `own@2` (staged upload), `prev@4` (previous frame complete), `image_acquired` at `eAllTransfer` stage mask
  - signals: `own@4` (frame complete, timeline) **and** `frame_completed_semaphores[fif]` (frame completion credential, binary — waited on by `Present`)
  - fence: `command_executed_fences[fif]`
- **AND** the batch is submitted even when the copy CB is `nullptr` (headless)

#### Scenario: SubmitFrame presents after the batch
- **WHEN** `SubmitFrame` has submitted the batch
- **THEN** it calls `m_present_provider->Present(device, framebuffer, frame_completed_semaphores[fif])`
- **AND** calls `pimpl->CompleteFrame()` (readback, frame-in-flight advance, timeline `EndFrame()`)
- **AND** returns the provider's `needs_recreating` result

#### Scenario: Timeline timepoint 3 is removed
- **WHEN** `SubmitFrame` builds the batch
- **THEN** the timeline signal is `own@4` only (value jumps 2 → 4); timepoint 3 ("render complete") is no longer produced or consumed — batch ordering replaces it

#### Scenario: FrameManager no longer touches Swapchain directly
- **WHEN** `SubmitFrame` executes
- **THEN** it does NOT call `m_system.GetSwapchain()` at any point
- **AND** `FrameManager` exposes `SubmitFrame` as its single frame-completion method (no separate submit/present methods)

#### Scenario: Headless multi-frame rendering does not deadlock
- **WHEN** a headless render loop runs for at least 4 frames with a readback registered
- **THEN** every `StartFrame` fence wait and every `prev@4` timeline wait is satisfied
- **AND** readback callbacks fire (the readback submit waits on `own@4`, which is unconditionally signaled by the frame-completion batch)

### Requirement: RenderGraph records only — submission belongs to the frame-completion point
`RenderGraph::RecordIntoMainCommandBuffer` SHALL record passes into the main command buffer without beginning, ending or submitting it. Submission SHALL happen via `RenderSystem::CompleteFrame` → `FrameManager::SubmitFrame`.

#### Scenario: RecordIntoMainCommandBuffer records only
- **WHEN** `RenderGraph::RecordIntoMainCommandBuffer` is called
- **THEN** it records all passes onto the current frame-in-flight main command buffer and returns
- **AND** it does NOT begin the command buffer (the caller began it via `FrameManager::BeginMainCommandBuffer`)
- **AND** it does NOT end the command buffer (`SubmitFrame` ends it)
- **AND** it does NOT submit — neither through the frame manager nor via `vkQueueSubmit2` (submission happens later in `CompleteFrame`)

#### Scenario: Caller submits via CompleteFrame
- **WHEN** a caller has recorded passes
- **THEN** it calls `RenderSystem::CompleteFrame(final_rtt, last_access)` to submit the frame-completion batch and present
- **AND** CPU-side post-processing (e.g. `physics->PostGPUStep`) runs after `CompleteFrame` (the batch is already submitted)

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

#### Scenario: Headless RunOneFrame does not crash
- **WHEN** `RunOneFrame` runs in headless mode
- **THEN** it does not dereference `input` or `window` (null in headless mode)
- **AND** it obtains the frame size from `GetPresentProvider().GetExtent()`

#### Scenario: Headless RenderSystem::CompleteFrame skips present
- **WHEN** `CompleteFrame` is called in headless mode
- **THEN** `HeadlessPresentProvider::PrepareCopy` returns `nullptr` and `Present` returns `false`
- **AND** no `vkQueuePresentKHR` is called
- **AND** the frame-completion batch still signals `own@4` + fence
