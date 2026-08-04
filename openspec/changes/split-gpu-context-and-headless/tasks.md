## 1. GpuContext DLL setup

- [x] 1.1 Create `engine/GpuContext/` directory, `gpu_context_export.h`, and `CMakeLists.txt` with `GPU_CONTEXT_DLL_EXPORTS` compile definition
- [x] 1.2 Add `add_subdirectory(GpuContext)` to `engine/CMakeLists.txt`
- [x] 1.3 Configure `EngineDepSdl` and `EngineDepVulkan` as dependencies; ensure `GpuContext` does NOT depend on any `EngineLib*` OBJECT libraries

## 2. Migrate files to GpuContext

- [x] 2.1 Move `MemoryTypes.h` from `engine/Render/Memory/` to `engine/GpuContext/`; update include guards and all `#include` references (~30 files)
- [x] 2.2 Move `MemoryAllocation.h/.cpp` from `engine/Render/Memory/` to `engine/GpuContext/`; update all references
- [x] 2.3 Move `DeviceInterface.h/.cpp` from `engine/Render/RenderSystem/` to `engine/GpuContext/`; update include paths to point at new location
- [x] 2.4 Move `AllocatorState.h/.cpp` from `engine/Render/RenderSystem/` to `engine/GpuContext/`; remove unused `#include "Render/ImageUtils.h"` from header
- [x] 2.5 Change `AllocatorState` constructor parameter from `RenderSystem&` to `DeviceInterface&`; update `Create()`, `AllocateBuffer()`, `AllocateImage()`, `QueryFormatFeatures()` to use `m_device_interface` instead of `m_system`
- [x] 2.6 Update all callers of `AllocatorState(RenderSystem&)` to pass `DeviceInterface&` instead (~8 call sites: `RenderSystem::Create`, `PhysicsScene::SyncGpuBuffers`, tests)
- [x] 2.7 Create `GpuContext.h/.cpp` aggregation class with `GetDevice()`, `GetAllocatorState()`, `GetDeviceInterface()` methods

## 3. IPresentProvider interface and implementations

- [ ] 3.1 Define `FrameSyncInfo` (two `std::array<vk::SemaphoreSubmitInfo, 2>` wait/signal + `vk::Fence`) in `engine/Render/RenderSystem/IPresentProvider.h`
- [ ] 3.2 Define `IPresentProvider` pure virtual interface: `GetExtent`, `GetColorFormat`, `GetImageCount`, `AcquireNextImage`, `CompleteFrame(vk::Device, const RenderTargetTexture&, uint32_t, const FrameSyncInfo&) → bool`, `Recreate`
- [ ] 3.3 Create `SwapchainPresentProvider.h/.cpp` — migrate swapchain lifecycle (`Recreate`/destroy), `AcquireNextImage` via `vkAcquireNextImageKHR`, owns `copy_to_swapchain_command_buffers` allocated from graphics pool in `Initialize()`, `CompleteFrame` records blit+barriers into own CB, `submit2(sync.wait, cb, sync.signal, sync.fence)`, `vkQueuePresentKHR` (wait copy_completed), returns needs-recreation bool
- [ ] 3.4 Create `HeadlessPresentProvider.h/.cpp` — `AcquireNextImage` returns `(counter++ % GetImageCount())`, `CompleteFrame` returns `false` (no-op), `GetExtent` returns config value, `Recreate` updates extent
- [ ] 3.5 Add both provider `.cpp` files to `EngineLibRender` OBJECT library sources

## 4. RenderSystem refactoring

- [x] 4.1 Remove `Swapchain m_swapchain` value member from `RenderSystem::impl`; remove `GetSwapchain()` public method
- [x] 4.2 Add `std::unique_ptr<IPresentProvider> m_present_provider` to `RenderSystem::impl`
- [x] 4.3 Add `GetPresentProvider()` public method returning `IPresentProvider&`
- [x] 4.4 Modify `RenderSystem::Create()`: create `SwapchainPresentProvider` when windowed, `HeadlessPresentProvider` when headless; inject into `FrameManager::Create()`
- [x] 4.5 Modify `RenderSystem::CompleteFrame()` to call `FrameManager::PresentToFramebuffer(...)` and `UpdateSwapchain()` when it returns `true`
- [x] 4.6 Modify `RenderSystem::UpdateSwapchain()` to delegate to `pimpl->m_present_provider->Recreate(new_extent)`

## 5. FrameManager refactoring

- [x] 5.1 Add `IPresentProvider* m_present_provider` member to `FrameManager::impl`
- [x] 5.2 Modify `FrameManager::Create()` to accept `IPresentProvider&` parameter and store pointer
- [x] 5.3 Replace `m_system.GetSwapchain().GetFrameCount()` with `m_present_provider->GetImageCount()` in semaphore initialization
- [x] 5.4 Replace `acquireNextImageKHR` in `StartFrame()` with `m_present_provider->AcquireNextImage(...)`
- [ ] 5.5 Restore `PresentToFramebuffer()` full logic: build `FrameSyncInfo` from internal timeline/image_acquired/copy_completed semaphores + command_executed fence, call `m_present_provider->CompleteFrame(device, src, fb, sync)`, then `pimpl->CompleteFrame()` (readback + FIF advance + timeline `EndFrame()`), return needs-recreation
- [ ] 5.6 **Revert** the incorrect FIF increment added at the end of `SubmitMainCommandBuffer()` (duplicates `impl::CompleteFrame()`; breaks timeline state machine)
- [ ] 5.7 **Remove** `GetImageAcquiredSemaphore()` added earlier (no longer exposed; `FrameSyncInfo` carries the semaphore)

## 6. Update Swapchain consumers

- [x] 6.1 `CommandBuffer.cpp:292`: change `m_system.GetSwapchain().GetExtent()` to `m_system.GetPresentProvider().GetExtent()`
- [x] 6.2 `ComplexRenderGraphBuilder.cpp:171`: verify it already uses explicit `texture_width`/`texture_height` from `BuildDefaultRenderGraph` params (remove swapchain extent read if present)
- [x] 6.3 `GUISystem.cpp:116,128`: change `render_system.GetSwapchain().GetColorFormat()` to `render_system.GetPresentProvider().GetColorFormat()`
- [x] 6.4 Update 6 test files that call `rsys->GetSwapchain().GetExtent()` to use `rsys->GetPresentProvider().GetExtent()` instead

## 7. Headless device and initialization

- [ ] 7.1 Modify `DeviceInterface` constructor: conditionally skip `CreateSurface()` when `cfg.window == nullptr`
- [ ] 7.2 Modify `CreateInstance`: skip surface extensions when headless
- [ ] 7.3 Modify `GetPhysicalDevice`: skip present queue and swapchain support checks when headless
- [ ] 7.4 Modify `CreateDevice`/`CreateCommandPool`: skip present queue/pool creation when headless
- [ ] 7.5 Ensure `SDL_Vulkan_LoadLibrary(nullptr)` is called in headless mode

## 8. MainClass headless path

- [ ] 8.1 Add `bool headless` field to `StartupOptions` in `OptionHandler.h`; add `--headless` CLI flag parsing in `OptionHandler.cpp`
- [ ] 8.2 Modify `MainClass::Initialize`: when `opt->headless`, skip `SDLWindow`, `GUISystem`, `Input` creation; pass empty `std::weak_ptr<SDLWindow>` to `RenderSystem`
- [ ] 8.3 Ensure `RenderSystem::Create()` detects null window and creates `HeadlessPresentProvider` with default/configured resolution

## 9. Cross-DLL dispatch loader initialization

- [ ] 9.1 Keep `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` in `engine/MainClass.cpp` (engine.dll's own dispatch loader copy)
- [ ] 9.2 In `RenderSystem::Create()`: initialize engine.dll's dispatch loader with `VULKAN_HPP_DEFAULT_DISPATCHER.init(di.GetInstance(), ::vkGetInstanceProcAddr)` then `VULKAN_HPP_DEFAULT_DISPATCHER.init(di.GetDevice())` (instance-level init first; `init(device)` alone crashes with DEP violation at null `vkGetDeviceProcAddr`)
- [ ] 9.3 Ensure `GpuContext` DLL has its own initialized dispatch loader via `DeviceInterface` (already done in `DeviceInterface.cpp`)

## 10. Headless compute shader test

- [ ] 10.1 Create `test/headless_compute_test.cpp`: inline GLSL compute shader, `GpuContext` headless creation, `ComputeStage::Instantiate`, `AllocatorState::AllocateBuffer`, dispatch, CPU readback, result verification
- [ ] 10.2 Add `headless_compute_test` target in `test/CMakeLists.txt`, link `engine` (for `ComputeStage`, `ComputeBuffer`, `ShaderCompiler`)
- [ ] 10.3 Verify test passes: `ctest --preset debug -R headless_compute_test`

## 11. Headless offscreen rendering test

- [ ] 11.1 Create `test/headless_offscreen_test.cpp`: `MainClass::Initialize` headless, `LoadBuiltinAssets`, `LoadProject`, `ComplexRenderGraphBuilder::BuildDefaultRenderGraph`, offscreen `RenderTargetTexture`, transfer pass (`vkCmdCopyImageToBuffer`), CPU readback, PPM file output
- [ ] 11.2 Add `headless_offscreen_test` target in `test/CMakeLists.txt`, link `engine`
- [ ] 11.3 Verify test passes: `ctest --preset debug -R headless_offscreen_test`; output file exists and contains valid PPM data

## 12. Build and reflection fixes

- [ ] 12.1 Update `engine/CMakeLists.txt`: add `PUBLIC GpuContext` to engine DLL's `target_link_libraries`; add POST_BUILD copy of `GpuContext.dll` to `build/bin/`
- [ ] 12.2 Fix reflection parser: update any hardcoded include paths referencing old `DeviceInterface.h`/`AllocatorState.h`/`MemoryTypes.h` locations
- [ ] 12.3 Run full build: `cmake --build --preset debug` — fix all compilation errors from moved files and changed signatures
- [ ] 12.4 Run existing test suite: `ctest --preset debug` — ensure no regressions in windowed mode (all 30 non-rendering tests + 14 rendering tests must pass)
