# headless-present-pipeline

## Purpose

Replace the no-op `HeadlessPresentProvider` with a real `OffscreenPresentProvider` that owns per-frame host-visible present targets (so rendered frames are no longer discarded and provide a CPU-readable surface), flow the headless extent from `StartupOptions` instead of a hard-coded resolution, and keep the `IPresentProvider` abstraction and `FrameManager` frame-completion model unchanged.

## REMOVED Requirements

### Requirement: HeadlessPresentProvider implements no-op copy and presentation

**Reason**: The no-op provider discarded every rendered frame and provided no CPU-accessible target. It is replaced by `OffscreenPresentProvider` (see ADDED requirement) which records a real copy into host-visible per-frame-in-flight targets and lazily allocates them, so mode-1 style headless loops that never render pay nothing.
**Migration**: `HeadlessPresentProvider` is renamed/upgraded to `OffscreenPresentProvider` and now records real copies. Any code that relied on `PrepareCopy` returning `nullptr` in headless mode must instead branch on whether rendering occurs (the provider allocates lazily and never records when unused).

## ADDED Requirements

### Requirement: OffscreenPresentProvider implements offscreen copy and presentation

`OffscreenPresentProvider` SHALL implement `IPresentProvider` for headless/offscreen rendering. It SHALL lazily allocate one host-visible present buffer per frame-in-flight (matching `GetImageCount()`) on first `PrepareCopy` call, and SHALL record a `copyImageToBuffer` from the final RTT into the buffer for the given image index with barriers derived from `last_access`. `Present` SHALL return `false` with no Vulkan presentation. A readback-callback API is NOT part of this change.

#### Scenario: AcquireNextImage returns synthetic index

- **WHEN** `OffscreenPresentProvider::AcquireNextImage` is called
- **THEN** it returns `(counter++) % GetImageCount()` and signals `image_ready_semaphore` via an empty `vkQueueSubmit2` submit

#### Scenario: Present targets are lazily allocated

- **WHEN** an offscreen render loop runs zero frames (no `PrepareCopy` call)
- **THEN** no present buffers are allocated (zero memory footprint)
- **WHEN** `PrepareCopy` is first called
- **THEN** `GetImageCount()` host-visible `ReadbackFromDevice` buffers of the present extent size are allocated and retained

#### Scenario: PrepareCopy records the offscreen copy

- **WHEN** `PrepareCopy` is called with the final RTT, an image index, and `last_access`
- **THEN** it records a `copyImageToBuffer` into the buffer for that image index
- **AND** the copy source layout/access is derived from `last_access` via `GetImageLayout` / `GetAccessFlags`
- **AND** it returns a non-null command buffer to be executed in the frame-completion batch

#### Scenario: Present is a no-op

- **WHEN** `OffscreenPresentProvider::Present` is called with any parameters
- **THEN** it returns `false` without performing any Vulkan presentation operations

#### Scenario: Extent and format come from construction

- **WHEN** `GetExtent` is called
- **THEN** it returns the extent provided at construction time (the `StartupOptions` resolution, not a hard-coded value)

#### Scenario: Recreate replaces the target sizing

- **WHEN** `Recreate` is called with a new extent
- **THEN** the stored extent is updated and future target allocations use the new size

## MODIFIED Requirements

### Requirement: RenderSystem owns IPresentProvider instead of Swapchain

`RenderSystem` SHALL own an `IPresentProvider` via `std::unique_ptr` instead of a `Swapchain` value member.

#### Scenario: RenderSystem::Create selects provider type

- **WHEN** `RenderSystem::Create` is called with a valid `SDLWindow`
- **THEN** it creates a `SwapchainPresentProvider` and stores it in `m_present_provider`
- **WHEN** `RenderSystem::Create` is called with a null window (headless)
- **THEN** it creates an `OffscreenPresentProvider` with the configured resolution, where the extent SHALL be the non-zero `extent` passed to the `RenderSystem` constructor (fed from `StartupOptions::resol_x/resol_y` in headless mode; a zero extent in headless mode SHALL throw because there is no window to derive one from), not a hard-coded 1920×1080

#### Scenario: RenderSystem exposes present provider

- **WHEN** `GetPresentProvider()` is called
- **THEN** it returns a reference to the owned `IPresentProvider`

#### Scenario: GetSwapchain() is removed

- **WHEN** code attempts to call `RenderSystem::GetSwapchain()`
- **THEN** compilation fails (method no longer exists)

#### Scenario: CompleteFrame parameters are simplified

- **WHEN** `RenderSystem::CompleteFrame` is called
- **THEN** it accepts only `(const RenderTargetTexture&, MemoryAccessTypeImageBits last_access)` — the `width`/`height`/`offset_x`/`offset_y` parameters are removed (the blit region derives from the provider extent)

### Requirement: Headless mode skips window-dependent subsystems

In headless mode, `MainClass::Initialize` SHALL not create `SDLWindow`, `GUISystem`, or `Input`.

#### Scenario: Headless Initialize

- **WHEN** `Initialize` is called with `StartupOptions::headless == true`
- **THEN** `SDLWindow` is not created (`this->window` remains `nullptr`)
- **AND** `RenderSystem` is constructed with an empty `std::weak_ptr<SDLWindow>` and the headless extent taken from `StartupOptions::resol_x`/`resol_y`
- **AND** `GUISystem` and `Input` are not created
- **AND** `RenderSystem::Create()` produces an `OffscreenPresentProvider`

#### Scenario: Headless RunOneFrame does not crash

- **WHEN** `RunOneFrame` runs in headless mode
- **THEN** it does not dereference `input` or `window` (null in headless mode)
- **AND** it obtains the frame size from `GetPresentProvider().GetExtent()`

#### Scenario: Headless RenderSystem::CompleteFrame skips present

- **WHEN** `CompleteFrame` is called in headless mode
- **THEN** `OffscreenPresentProvider::PrepareCopy` records an offscreen copy (lazily allocating targets) and `Present` returns `false`
- **AND** no `vkQueuePresentKHR` is called
- **AND** the frame-completion batch still signals `own@4` + fence
