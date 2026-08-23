# Proposal: Resizable Render Targets

## Why

On window resize the render graph textures are created once at a fixed size (the initial swapchain/window size) while the swapchain is recreated at the new size. The present blit always copies a source region equal to the swapchain extent (`SwapchainPresentProvider::RecordCopyCommand`), so enlarging the window reads out of bounds from the undersized final texture (clamping to the edge color / smearing) and shrinking it crops the image. Minimizing the window can also report a 0×0 pixel size, which crashes on the next `ResizableRTTManager` resolve (`assert(reference_width > 0)`). The engine already has the infrastructure to make render target textures follow the swapchain size (`ResizableRTTManager` + `RequestResizableRenderTargetTexture`), but the default render graph builder and the runtime loops do not use it.

## What Changes

- **BREAKING** `ComplexRenderGraphBuilder::BuildDefaultRenderGraph` no longer takes `texture_width`/`texture_height`. All render-time dimensions (viewport, bloom dispatch) are derived at record time.
- `ComplexRenderGraphBuilder` requests the Final Color, HDR Color and Depth textures as resizable render targets (scale 1.0), so they stay equal to the swapchain size and the present blit is always 1:1.
- The bloom compute dispatch count is computed at record time from the resolved HDR texture description instead of a build-time size.
- `RenderSystem::UpdateSwapchain` skips swapchain/RTT recreation when the window size is 0×0 (minimized), preventing an invalid 0-size swapchain and the reference-size assert.
- `ResizableRTTManager::SetReferenceSize` ignores a 0 width/height and keeps the last valid reference size.
- The runtime loops that drive a window (`MainClass::RunOneFrame` and `PhysicsApp::RenderNextFrame`) update the active camera's aspect ratio to match the current present extent when it changes.
- `PhysicsApp` mirrors the editor `SceneWidget` camera behavior: when the aspect crosses 1.0 it switches between a fixed vertical and fixed horizontal FOV so the perceived zoom stays stable in both landscape and portrait.
- `MainClass::RunOneFrame` updates only the aspect ratio (fixed FOV); the generic main loop intentionally does not change FOV semantics.

## Capabilities

### New Capabilities

- `resizable-render-targets`: The default render graph and runtime loops keep the presentable frame consistent with the swapchain across window resizes — textures follow the swapchain size (no clamp/crop on present), degenerate 0×0 sizes are handled safely, and the active camera's aspect ratio follows the window.

### Modified Capabilities

<!-- None: no existing requirement changes; this is a new engine-level capability. -->

## Impact

- **Engine API (breaking)**: `ComplexRenderGraphBuilder::BuildDefaultRenderGraph` signature (remove `texture_width`, `texture_height`).
- **Call sites updated**: `app/physics/PhysicsApp.cpp`, `example/external_resource_loading_example/main.cpp`, `test/project_loading_test.cpp`, `test/input_test.cpp`.
- **Engine files**:
  - `engine/Framework/Tools/ComplexRenderGraphBuilder.{h,cpp}` — resizable textures, record-time sizes, signature.
  - `engine/Render/RenderSystem.cpp` — 0×0 guard in `UpdateSwapchain`.
  - `engine/Render/RenderSystem/ResizableRTTManager.{h,cpp}` — 0-size semantics + doc.
  - `engine/Framework/MainClass.{h,cpp}` — active camera aspect sync in `RunOneFrame`.
  - `app/physics/PhysicsApp.{h,cpp}` — aspect + FOV behavior in `RenderNextFrame`.
- **Out of scope** (recorded to avoid scope creep):
  - The editor's own `EditorRenderGraphBuilder` (separate fixed-size widget textures, distinct work item).
  - Shadow map targets (stays fixed size).
  - Bloom internals (currently a placeholder; only the dispatch count bug is fixed).
  - Camera system redesign (shared_ptr registration / `display_id` routing is known-flawed and tracked separately; this change only adds a thin per-loop aspect sync that is easy to replace).
