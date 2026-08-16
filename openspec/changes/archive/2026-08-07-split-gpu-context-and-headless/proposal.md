## Why

The engine's GPU compute and rendering capabilities are tightly coupled to `RenderSystem`, which mandates a window, Vulkan surface, and swapchain. This prevents running GPU physics simulations or offscreen rendering on headless machines (no display). The `RenderSystem` god object also makes it impossible to use GPU resources without the full rendering pipeline.

## What Changes

- **New `GpuContext` DLL**: Extract `DeviceInterface`, `AllocatorState`, `MemoryTypes`, and `MemoryAllocation` into a standalone DLL (like `Core`), independent of the Render module. AllocatorState's constructor changes from `RenderSystem&` to `DeviceInterface&`.

- **`IPresentProvider` interface**: Introduce a Strategy pattern interface abstracting frame acquisition and presentation. Two implementations: `SwapchainPresentProvider` (windowed, existing behavior) and `HeadlessPresentProvider` (no-op present, synthetic frame index).

- **True headless Vulkan device**: `DeviceInterface` supports creating a Vulkan device without a surface when no `SDL_Window*` is provided. Instance extensions, surface creation, and present queue are conditionally skipped. **BREAKING**: `RenderSystem::GetSwapchain()` removed; swapchain state moves into `SwapchainPresentProvider`.

- **Headless rendering support**: `FrameManager` works without swapchain via `IPresentProvider`. `CommandBuffer::DrawRenderers` and `ComplexRenderGraphBuilder` read extent from `IPresentProvider` instead of swapchain.

- **Headless offscreen rendering test**: Loads a project scene, renders one frame to an offscreen `RenderTargetTexture`, copies to a host-visible buffer via `vkCmdCopyImageToBuffer`, and outputs a PPM file — all without a window or surface.

- **Headless GPU compute test**: Verifies `GpuContext` standalone capability by compiling an inline GLSL compute shader and dispatching it without `RenderSystem`.

## Capabilities

### New Capabilities
- `gpu-context-module`: Standalone `GpuContext` DLL containing `DeviceInterface`, `AllocatorState`, `MemoryTypes`, `MemoryAllocation`, and `GpuContext` aggregation class.
- `headless-present-pipeline`: `IPresentProvider` interface with `SwapchainPresentProvider` and `HeadlessPresentProvider` implementations, enabling swapchain-free rendering in `FrameManager`.
- `headless-vulkan-device`: `DeviceInterface` supports creating a Vulkan device without `SDL_Window*`, skipping surface/surface-extension/present-queue creation.

### Modified Capabilities
*(None — all changes are new capabilities; no existing spec requirements are being altered.)*

## Impact

- **GpuContext module**: New shared library (`engine/GpuContext/`), linked by `engine.dll` and standalone tests. Depends on Vulkan SDK, VMA, SDL3 (loadlibrary only), and `Core`.
- **RenderSystem**: Loses direct ownership of `Swapchain` and `GetSwapchain()` accessor. Gains `IPresentProvider` ownership. `Create()` accepts optional headless config.
- **FrameManager**: Acquire/present logic replaced by `IPresentProvider` calls.
- **MainClass::Initialize**: Headless path skips `SDLWindow`, `GUISystem`, `Input` creation.
- **DeviceInterface**: Constructor conditionally skips surface-dependent steps.
- **AllocatorState**: Constructor parameter changes from `RenderSystem&` to `DeviceInterface&` (all call sites updated).
- **Test infrastructure**: New `headless_compute_test` and `headless_offscreen_test` targets.
