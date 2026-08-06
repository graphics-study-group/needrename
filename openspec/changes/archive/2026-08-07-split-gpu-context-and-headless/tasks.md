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

- [x] 3.1 Define `IPresentProvider` pure virtual interface in `engine/Render/RenderSystem/IPresentProvider.h`: `GetExtent`, `GetColorFormat`, `GetImageCount`, `AcquireNextImage(vk::Device, vk::Semaphore image_ready_semaphore, uint64_t)`, `PrepareCopy(vk::Device, const RenderTargetTexture&, uint32_t, MemoryAccessTypeImageBits) → vk::CommandBuffer`, `Present(vk::Device, uint32_t, vk::Semaphore frame_done_semaphore) → bool`, `Recreate`. NO `FrameSyncInfo`, NO sync arrays, NO fence parameters. The credential is a BINARY semaphore (`frame_completed_semaphores[fif]`), because `vkQueuePresentKHR` accepts only binary semaphores
- [x] 3.2 Create `SwapchainPresentProvider.h/.cpp` — swapchain lifecycle (`Initialize`/`Recreate`), `AcquireNextImage` via `vkAcquireNextImageKHR` (returns `~0u` on `VK_ERROR_OUT_OF_DATE_KHR`), owns one copy command buffer per swapchain image (graphics pool), `PrepareCopy` records blit with barriers derived from `last_access` (no submit), `Present` calls `vkQueuePresentKHR` waiting on the frame completion credential, returns needs-recreation bool
- [x] 3.3 Create `HeadlessPresentProvider.h/.cpp` — constructor `(const DeviceInterface&, vk::Extent2D, vk::Format, uint32_t)` (the `DeviceInterface&` is needed to signal `image_ready_semaphore` via an empty submit on the graphics queue), `AcquireNextImage` returns `(counter++ % GetImageCount())` and signals `image_ready_semaphore` via an empty `submit2`, `PrepareCopy` returns `nullptr`, `Present` returns `false`, `GetExtent` returns config value, `Recreate` updates extent
- [x] 3.4 Add both provider `.cpp` files to `EngineLibRender` OBJECT library sources
- [x] 3.5 Delete the old `Swapchain.h/.cpp` class (dead code after provider migration) and its 4 stale includes (`GUISystem.cpp`, `CommandBuffer.cpp`, `MaterialTemplate.cpp`, `FullRenderSystem.h`)
- [x] 3.6 Delete `Render/RenderSystem/Structs.h` (byte-identical duplicate of `GpuContext/Structs.h`); repoint its 3 includes (`GUISystem.cpp`, `SubmissionHelper.cpp`, `FullRenderSystem.h`) to `GpuContext/Structs.h`

## 4. RenderSystem refactoring

- [x] 4.1 Remove `Swapchain m_swapchain` value member from `RenderSystem::impl`; remove `GetSwapchain()` public method
- [x] 4.2 Add `std::unique_ptr<IPresentProvider> m_present_provider` to `RenderSystem::impl`
- [x] 4.3 Add `GetPresentProvider()` public method returning `IPresentProvider&`
- [x] 4.4 Modify `RenderSystem::Create()`: create `SwapchainPresentProvider` when windowed, `HeadlessPresentProvider` when headless; inject into `FrameManager::Create()`
- [x] 4.5 Modify `RenderSystem::CompleteFrame()` to delegate to `FrameManager::SubmitFrame(...)` and call `UpdateSwapchain()` when it returns `true`
- [x] 4.6 Modify `RenderSystem::UpdateSwapchain()` to delegate to `pimpl->m_present_provider->Recreate(new_extent)`

## 5. FrameManager refactoring

- [x] 5.1 Add `IPresentProvider* m_present_provider` member to `FrameManager::impl`
- [x] 5.2 Modify `FrameManager::Create()` to accept `IPresentProvider&` parameter and store pointer
- [x] 5.3 Remove `copy_to_swapchain_completed_semaphores` (subsumed by `timeline@4`); acquire semaphores sized by `FRAMES_IN_FLIGHT` as before
- [x] 5.4 Replace `acquireNextImageKHR` in `StartFrame()` with `m_present_provider->AcquireNextImage(...)`
- [x] 5.5 Implement `SubmitFrame(present_texture, last_access)`: end main CB → `PrepareCopy` (may be `nullptr`) → submit ONE batch `{main CB, copy CB}` with waits `{own@2, prev@4, image_acquired @eAllTransfer}`, signals `{own@4, frame_completed_semaphores[fif]}`, fence `command_executed_fences[fif]` → `Present(device, fb, frame_completed_semaphores[fif])` → `impl::CompleteFrame()` → return needs-recreation. Delete `SubmitMainCommandBuffer` and `PresentToFramebuffer` (absorbed)
- [x] 5.6 Remove timeline timepoint 3 ("render complete") — only timepoints 1/2/4 remain; `SetExpectedTimepoints(4)` stays, signal value jumps 2 → 4
- [x] 5.7 Remove `GetImageAcquiredSemaphore()` if present (no longer needed as a public accessor)
- [x] 5.8 In `StartFrame()`, handle `~0u` from `AcquireNextImage`: trigger swapchain recreation (`UpdateSwapchain`) and retry acquisition once; never use `~0u` as a framebuffer index
- [x] 5.9 Add `BeginMainCommandBuffer()` returning a begun main CB (begin/end lifecycle converges on FrameManager; `SubmitFrame` ends it)

## 6. Update Swapchain consumers

- [x] 6.1 `CommandBuffer.cpp:292`: change `m_system.GetSwapchain().GetExtent()` to `m_system.GetPresentProvider().GetExtent()`
- [x] 6.2 `ComplexRenderGraphBuilder.cpp:171`: verify it already uses explicit `texture_width`/`texture_height` from `BuildDefaultRenderGraph` params (remove swapchain extent read if present)
- [x] 6.3 `GUISystem.cpp:116,128`: change `render_system.GetSwapchain().GetColorFormat()` to `render_system.GetPresentProvider().GetColorFormat()`
- [x] 6.4 Update 6 test files that call `rsys->GetSwapchain().GetExtent()` to use `rsys->GetPresentProvider().GetExtent()` instead
- [x] 6.5 Change `RenderGraph::Execute` to record only (begin / RecordAllPasses / end, no submit; consider renaming to `RecordIntoMainCommandBuffer`); update its call sites (`headless_offscreen_test` and any examples) to call `RenderSystem::CompleteFrame` afterwards
- [x] 6.6 Simplify `RenderSystem::CompleteFrame` to `(const RenderTargetTexture&, MemoryAccessTypeImageBits last_access)` — drop `width`/`height`/`offset_x`/`offset_y`; update all call sites (MainClass, tests)

## 7. Headless device and initialization

- [x] 7.1 Modify `DeviceInterface` constructor: conditionally skip `CreateSurface()` when `cfg.window == nullptr`
- [x] 7.2 Modify `CreateInstance`: skip surface extensions when headless
- [x] 7.3 Modify `GetPhysicalDevice`: skip present queue and swapchain support checks when headless
- [x] 7.4 Modify `CreateDevice`/`CreateCommandPool`: skip present queue/pool creation when headless
- [x] 7.5 Ensure `SDL_Vulkan_LoadLibrary(nullptr)` is called in headless mode

## 8. MainClass headless path

- [x] 8.1 Add `bool headless` field to `StartupOptions` in `OptionHandler.h`; add `--headless` CLI flag parsing in `OptionHandler.cpp`
- [x] 8.2 Modify `MainClass::Initialize`: when `opt->headless`, skip `SDLWindow`, `GUISystem`, `Input` creation; pass empty `std::weak_ptr<SDLWindow>` to `RenderSystem`
- [x] 8.3 Ensure `RenderSystem::Create()` detects null window and creates `HeadlessPresentProvider` with default/configured resolution

## 9. Cross-DLL dispatch loader initialization

- [x] 9.1 Keep `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` in `engine/MainClass.cpp` (engine.dll's own dispatch loader copy)
- [x] 9.2 In `RenderSystem::Create()`: initialize engine.dll's dispatch loader with `VULKAN_HPP_DEFAULT_DISPATCHER.init(di.GetInstance(), ::vkGetInstanceProcAddr)` then `VULKAN_HPP_DEFAULT_DISPATCHER.init(di.GetDevice())` (instance-level init first; `init(device)` alone crashes with DEP violation at null `vkGetDeviceProcAddr`)
- [x] 9.3 Ensure `GpuContext` DLL has its own initialized dispatch loader via `DeviceInterface` (already done in `DeviceInterface.cpp`)

## 10. Headless compute shader test

- [x] 10.1 Create `test/headless_compute_test.cpp`: inline GLSL compute shader, `GpuContext` headless creation, `ComputeStage::Instantiate`, `AllocatorState::AllocateBuffer`, dispatch, CPU readback, result verification
- [x] 10.2 Add `headless_compute_test` target in `test/CMakeLists.txt`, link `engine` (for `ComputeStage`, `ComputeBuffer`, `ShaderCompiler`)
- [x] 10.3 Verify test passes: `ctest --preset debug -R headless_compute_test`

## 11. Headless offscreen rendering test

- [x] 11.1 Create `test/headless_offscreen_test.cpp`: `MainClass::Initialize` headless, `LoadBuiltinAssets`, `LoadProject`, `ComplexRenderGraphBuilder::BuildDefaultRenderGraph`, offscreen `RenderTargetTexture`, transfer pass (`vkCmdCopyImageToBuffer`), CPU readback, PPM file output
- [x] 11.2 Add `headless_offscreen_test` target in `test/CMakeLists.txt`, link `engine`
- [x] 11.3 Verify test passes: `ctest --preset debug -R headless_offscreen_test`; output file exists and contains valid PPM data

## 12. Build and reflection fixes

- [x] 12.1 Update `engine/CMakeLists.txt`: add `PUBLIC GpuContext` to engine DLL's `target_link_libraries`; add POST_BUILD copy of `GpuContext.dll` to `build/bin/`
- [x] 12.2 Fix reflection parser: update any hardcoded include paths referencing old `DeviceInterface.h`/`AllocatorState.h`/`MemoryTypes.h` locations
- [x] 12.3 Run full build: `cmake --build --preset debug` — fix all compilation errors from moved files and changed signatures
- [x] 12.4 Run existing test suite: `ctest --preset debug` — ensure no regressions in windowed mode (all 30 non-rendering tests + 14 rendering tests must pass)

## 13. Frame-completion batch follow-ups (review session outcomes)

- [x] 13.1 Fix `MainClass::RunOneFrame` headless crash: guard `input`/`window` access when headless; obtain frame size from `GetPresentProvider().GetExtent()`
- [x] 13.2 Add multi-frame headless test (≥4 frames + one readback): verifies the sync chain closes without deadlock (single-frame tests cannot catch it)
- [x] 13.3 Ensure `image_ready` wait uses `eAllTransfer` stage mask (stage latch, NOT `eAllCommands` — would gate the whole batch); add a comment at the submit site
- [x] 13.4 Verify timeline timepoint-3 consumers are gone (grep `GetSubmitInfo(3` / `ExpectedTimepoints() - 1`) before merging
- [x] 13.5 Restore `vk::Filter::eLinear` for the present blit (regression vs. the old default `eLinear`)

## 14. Standalone GpuContext verification (verify-change WARNING follow-up)

- [x] 14.1 Create `test/gpu_context_standalone_test.cpp`: constructs `Engine::GpuContext` directly with `DeviceConfiguration{.window = nullptr}` (no RenderSystem, no surface), compiles an inline GLSL compute shader via `ShaderCompiler`, builds a minimal raw-Vulkan compute pipeline, allocates the output buffer via `AllocatorState::AllocateBuffer(ReadbackFromDevice)`, dispatches, and verifies the readback — fulfilling the `gpu-context-module` spec scenario "Standalone headless compute test"
- [x] 14.2 Add `gpu_context_standalone_test` target in `test/CMakeLists.txt`, link `engine` (for `ShaderCompiler`); initialize the module's dynamic dispatch loader copy (instance first, then device — same pattern as `RenderSystem::Create`)
- [x] 14.3 Verify test passes: `ctest --preset debug -R gpu_context_standalone_test`
- [x] 14.4 Skip the modal `SDL_ShowSimpleMessageBox` when `cfg.window == nullptr` (headless/CI environments would block forever) in `DeviceInterface::GetPhysicalDevice` failure path
