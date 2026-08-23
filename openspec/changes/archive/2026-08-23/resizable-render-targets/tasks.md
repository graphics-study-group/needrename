# Tasks: Resizable Render Targets

## 1. ComplexRenderGraphBuilder (engine)

- [x] 1.1 Change `BuildDefaultRenderGraph` signature to remove `texture_width`/`texture_height` (keep `final_color_target_id`, `model_matrices_buffer`) in `engine/Framework/Tools/ComplexRenderGraphBuilder.{h,cpp}`
- [x] 1.2 Request Final Color, HDR Color, and Depth via `RequestResizableRenderTargetTexture(desc, sampler, 1.0f, 1.0f, name)`; keep `RenderTargetTextureDesc.width/height` fields but they are ignored by the manager
- [x] 1.3 Update the main lit pass pass-function to size the viewport/scissor from `GetPresentProvider().GetExtent()` at record time (verify it already does; adjust only if stale)
- [x] 1.4 Update the bloom compute dispatch to derive `width/height` at record time from `GetInternalTextureResource(hdr_id)->GetTextureDescription()` instead of the removed parameters
- [x] 1.5 Remove any other build-time use of the removed `texture_width/height` parameters (e.g., dispatch lambda captures)

## 2. Update call sites for the signature change

- [x] 2.1 `app/physics/PhysicsApp.cpp` — call `BuildDefaultRenderGraph(final_color_id, model_matrices)` without w/h
- [x] 2.2 `example/external_resource_loading_example/main.cpp` — drop `w, h` args and the `GetWindow()->GetSize()` destructure if now unused
- [x] 2.3 `test/project_loading_test.cpp` — drop `w, h` args
- [x] 2.4 `test/input_test.cpp` — drop `w, h` args

## 3. 0×0 defensive hardening

- [x] 3.1 `engine/Render/RenderSystem.cpp` `UpdateSwapchain` — early-return when `w <= 0 || h <= 0` (skip `Recreate` and `SetReferenceSize`)
- [x] 3.2 `engine/Render/RenderSystem/ResizableRTTManager.cpp` `SetReferenceSize` — ignore `0` width/height, keep last valid reference size
- [x] 3.3 Add `@note` doc on `ResizableRTTManager::SetReferenceSize` documenting the 0-size ignore semantics in `ResizableRTTManager.h`

## 4. Camera aspect sync

- [x] 4.1 `engine/Framework/MainClass.{h,cpp}` — track last present extent; in `RunOneFrame` before `StartFrame`, when extent changed, call `world->GetActiveCamera()->set_aspect_ratio(w/h)` (no FOV change)
- [x] 4.2 `app/physics/PhysicsApp.{h,cpp}` — add `m_camera_fov` baseline (read from the camera's `m_fov_vertical` in `Create`) and a last-extent tracker; in `RenderNextFrame` before `StartFrame`, on extent change update aspect and apply the SceneWidget FOV switch (aspect > 1 → `set_fov_vertical(baseline)` else `set_fov_horizontal(baseline)`)

## 5. Build & regression verification

- [x] 5.1 Build with `cmake --build --preset debug` (env per `docs/build_instructions/windows_msys2_clang64.md`)
- [x] 5.2 Run `ctest --preset debug` — `pbr_test` (resizable-RTT anchor), `project_loading_test`, `input_test` stay green; no new failures

## 6. Manual resize acceptance (example/physics_example)

- [x] 6.1 Enlarge window beyond initial size — full-window scene, no edge-clamp smear
- [x] 6.2 Shrink window — full-window scene, no crop/compression
- [x] 6.3 Minimize then restore — no crash/assert; rebuilds at restored size and continues rendering
- [x] 6.4 Rapid continuous resizing — no VMA assertion, no validation-layer errors
- [x] 6.5 Portrait aspect — PhysicsApp FOV switch keeps a stable view (landscape/portrait)
