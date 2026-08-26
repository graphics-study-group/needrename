## 1. Engine: generic image-to-buffer copy recorder

- [x] 1.1 Implement `CommandBuffer::RecordCopyImageToBuffer(const RenderTargetTexture&, const Rhi::DeviceBuffer& dst, Rhi::MemoryAccessTypeImageBits last_access)` in `CommandBuffer.cpp`: pre-barrier `GetImageLayout(last_access) → eTransferSrcOptimal` (access `GetAccessFlags(last_access) → eTransferRead`), `copyImageToBuffer` with extent from `GetTextureDescription()` (mip 0, color aspect), post-barrier back to `GetImageLayout(last_access)`. `OffscreenPresentProvider` keeps its own local copy in an anonymous namespace (it records on a raw `vk::CommandBuffer`, with no `CommandBuffer` wrapper). Mirror the `SwapchainPresentProvider::RecordCopyCommand` barrier recipe.
- [x] 1.2 Verify it compiles and is usable from Render-layer code (consumers added later in this change).

## 2. Engine: FrameManager precise wait

- [x] 2.1 Record the last submitted fence in `FrameManager::impl` (the `command_executed_fences[fif]` signaled in `SubmitFrame`) at submit time — not looked up via `GetFrameInFlight()` (the counter advances inside `pimpl->CompleteFrame()`).
- [x] 2.2 Add `FrameManager::WaitForFrameCompletion() const`: no-op if no frame ever submitted; otherwise `device.waitForFences` on the recorded fence with unbounded timeout.

## 3. Engine: render extent flows from StartupOptions

- [x] 3.1 `RenderSystem` constructor gains a unified `vk::Extent2D extent = {0, 0}` parameter, stored in `pimpl`.
- [x] 3.2 `RenderSystem::Create` resolves the extent: windowed mode derives via `SDL_GetWindowSizeInPixels` when either component is 0; headless mode requires a non-zero extent or throws `std::logic_error` (no window to derive one from, replacing the hard-coded `{1920, 1080}`).
- [x] 3.3 `MainClass::Initialize` passes `StartupOptions::resol_x/resol_y` as the extent in headless mode; windowed mode passes `{0, 0}` (derived from the window).

## 4. Engine: OffscreenPresentProvider replaces HeadlessPresentProvider

- [x] 4.1 Add `OffscreenPresentProvider.h/.cpp` (renaming/upgrading `HeadlessPresentProvider`; remove the old class and its includes/builder references).
- [x] 4.2 Implement lazy allocation: on first `PrepareCopy`, allocate `GetImageCount()` host-visible `ReadbackFromDevice` buffers sized to the current extent (via `GetAllocatorState`); retain them.
- [x] 4.3 `PrepareCopy` records `copyImageToBuffer` (its local copy of the 1.1 recorder) into the buffer for `image_index` and returns the recorded command buffer; source layout derived via `GetImageLayout(last_access)`.
- [x] 4.4 Keep `AcquireNextImage` synthetic index + empty-submit signaling; `Present` returns `false` no-op; `Recreate` updates stored extent.
- [x] 4.5 Update `RenderSystem::Create` headless branch to construct `OffscreenPresentProvider`; delete old `HeadlessPresentProvider` sources from the build.
- [x] 4.6 Build and confirm existing headless tests (`headless_offscreen_test`, `headless_compute_test`) still pass.

## 5. PhysicsApp: AppMode and mode-1 (PhysicsOnly) simplification

- [x] 5.1 Add `enum class AppMode { PhysicsOnly, Offscreen, Windowed }` and `CreateInfo::mode` (default `Windowed`) in `PhysicsApp.h`.
- [x] 5.2 `PhysicsApp::Create` branches by mode: `PhysicsOnly`/`Offscreen` set `StartupOptions.headless = true`; `PhysicsOnly` skips camera GameObject/CameraComponent/CameraController/SetActiveCamera/initial `SetCameraPose`.
- [x] 5.3 `SceneBuilder` constructor gains `bool with_visuals`; when false: skip builtin mesh asset loads, and `AddBox/AddSphere/AddCylinder` skip mesh child creation and color-string resolution (physics assembly unchanged).
- [x] 5.4 `CommitScene` in `PhysicsOnly` mode skips `BuildDefaultRenderGraph` and the model-matrices bridge.
- [x] 5.5 Mode-specific throws: in `PhysicsOnly` mode `RenderNextFrame`, `GetRenderOutput`, `SetRenderReadbackEnabled(true)`, `AddDirectionalLight`, and `SetCameraPose` throw `std::logic_error`.
- [x] 5.6 `ShouldQuit` returns `false` in `PhysicsOnly`/`Offscreen` modes (no SDL_QUIT source).

## 6. PhysicsApp: physics state readback

- [x] 6.1 `SceneBuilder` exposes `GetBodyCount()` and `GetBodyHandle(BodyId)` (out-of-range throws `std::out_of_range`).
- [x] 6.2 In `CommitScene` (after `FlushPhysics`), build `body_id → slot_index` mapping via `PhysicsAdaptor::FindRigidBodyByObjectHandle`; assert every body resolves and entries are unique.
- [x] 6.3 At `CommitScene` end, allocate four `ReadbackFromDevice` `DeviceBuffer`s of `slot_count × sizeof(glm::vec4)` (position / rotation / linear / angular velocity) via `GetAllocatorState`; collect per-slot `com_offsets` from `PhysicsAdaptor::GetComOffsetLocal`.
- [x] 6.4 Extract `RecordBodyStateCopy(vk::CommandBuffer)` (four `copyBuffer` with `min(src, dst)` sizes); run it in a one-time submit+fence-wait at `CommitScene` end (seeds initial state) and reuse it inside `Step` after `GPUStep`.
- [x] 6.5 Implement `BodyState` struct, `BodyStatesView` struct (`slot_indices`, `com_offsets`, `positions`, `rotations`, `linear_velocities`, `angular_velocities` — vec4 SoA spans + vec3 com offsets), `GetBodyState(BodyId)` (assembles vec3/quat; `std::out_of_range` on bad id) and `GetBodyStates()`; both throw `std::logic_error` before `CommitScene`.

## 7. PhysicsApp: opt-in render readback

- [x] 7.1 Add `SetRenderReadbackEnabled(bool)` (default false) with staging lifecycle: allocate staging (extent-scaled, `ReadbackFromDevice`) on first enable, rebuild when present extent changes, retain on disable.
- [x] 7.2 `RenderNextFrame`: when enabled (and extent matches), record `cb.RecordCopyImageToBuffer` into the main CB after `RecordAllPasses` and record the current frame id.
- [x] 7.3 Add `RenderOutput { const void* pixels, uint32_t width, uint32_t height, uint64_t frame_id }` and `GetRenderOutput()`: validate (phase/mode/enabled/captured) → `FrameManager::WaitForFrameCompletion()` → map staging → return struct; skipped frames return the previous capture.
- [x] 7.4 `RenderNextFrame` mode branch: `PhysicsOnly` throws; `Offscreen` skips the input section (SDL polling, input update, SPACE toggle); `Windowed` unchanged.

## 8. Tests and example migration

- [x] 8.1 Remove `example/physics_example` (main.cpp, its CMake registration, and the directory).
- [x] 8.2 Add `test/physics_app_physics_only_test.cpp`: mode-1 physics readback (initial state legal before first step, mapping length/unique, step advances position under gravity), negative throws (RenderNextFrame/GetRenderOutput/SetRenderReadbackEnabled(true)/AddDirectionalLight/SetCameraPose).
- [x] 8.3 Add `test/physics_app_offscreen_test.cpp`: mode-2 render readback (disabled `GetRenderOutput` throws; enable → render → non-null pixels at create resolution, frame_id increases, non-all-zero content from sky+scene).
- [x] 8.4 Add `test/physics_app_windowed_test.cpp`: relocate scene builders (template scenes, double pendulum from the example); with frame-count arg run that many frames then exit and auto-`Resume()` (ctest), without args run indefinitely starting paused (SPACE resumes) (manual).
- [x] 8.5 Update `test/CMakeLists.txt`: three executables linking `Engine` + `PhysicsApp` (`PhysicsApp` target), registered with `add_test` (windowed with a finite frame arg, e.g. 120).
- [x] 8.6 Build and run `ctest --preset debug`; all three new tests plus existing headless tests pass.
