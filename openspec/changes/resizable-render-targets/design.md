# Design: Resizable Render Targets

## Context

The engine presents a render graph's Final Color texture to the swapchain via a 1:1 blit whose source region equals the swapchain extent (`SwapchainPresentProvider::RecordCopyCommand`). The implicitly expected invariant is `final texture size == swapchain size`, but the default graph builder (`ComplexRenderGraphBuilder::BuildDefaultRenderGraph`) creates its Final Color / HDR Color / Depth textures once at build time at a caller-provided size. On resize the swapchain is recreated (`RenderSystem::UpdateSwapchain`) but the graph textures are not, so the blit reads outside the final texture (clamped edge smear when enlarged) or reads a sub-rect (crop when shrunk).

The engine already contains the mechanism to keep textures aligned with the swapchain: `ResizableRTTManager` holds a reference size, `RequestRenderTargetTexture`'s resizable sibling (`RequestResizableRenderTargetTexture`) creates lazily-resolved textures sized `reference_size × scale`, and `UpdateSwapchain` already calls `SetReferenceSize`. The render graph resolves such handles lazily at record time (barriers, `WrapRenderPass` attachments, and per-pass resource lookups all resolve through the variant visitor), so a resize can be picked up without rebuilding the graph. `pbr_test` already exercises this resizable path today.

Two additional facts drive this design:
- The camera system is known to be flawed (weak/shared-ptr registration in `CameraManager`, `WorldSystem::GetActiveCamera` returning `shared_ptr<Camera>`, `display_id` routing). A redesign is planned; this change must not deepen that coupling.
- Bloom is a placeholder single-pass implementation; only a real bug in its dispatch sizing is fixed here.

## Goals / Non-Goals

**Goals:**
- Window resize keeps the presented image correct: full window coverage, no edge-clamp smear, no crop/compression.
- The active camera's aspect ratio follows the window size.
- Minimizing and restoring the window is safe (no 0×0 crash, clean rebuild on restore).
- Default graph construction no longer embeds a "first window only" size.
- All `ComplexRenderGraphBuilder` consumers benefit from resize correctness for free.

**Non-Goals:**
- Editor's own `EditorRenderGraphBuilder` (separate fixed-size widget textures).
- Shadow map targets (stay at fixed size).
- Bloom rewrite / shader edge hardening (bloom is a placeholder; only the dispatch-count bug is fixed).
- Camera system redesign (`shared_ptr` registration / `display_id` routing).
- Aspect handling for non-active cameras.

## Decisions

### D1. Use resizable RTTs (scale 1.0) instead of rebuilding the graph or a full-screen-RT + sub-rect blit

Final Color, HDR Color, and Depth become `RequestResizableRenderTargetTexture(..., 1.0f, 1.0f, ...)`. Their size then equals the `ResizableRTTManager` reference size, which `UpdateSwapchain` keeps equal to the window/swapchain size.

- *Alternative: rebuild the whole graph on resize* — more moving parts; builder lambdas capture state, and each resize re-allocates all textures and recompiles barriers.
- *Alternative: full-screen RT + present a sub-rect via UV (the SceneWidget pattern)* — mirrors the editor's in-window viewport, but the engine present path has no sub-rect blit; it would require `RecordCopyCommand` surgery and wastes memory. Not needed for a full-window app.
- *Why resizable RTT:* infra exists and is already tested; present blit stays 1:1 with zero engine present changes; textures lazily recreate on the next record-time resolve.

### D2. Drop `texture_width`/`texture_height` from `BuildDefaultRenderGraph`

All render-time dimensions come from record-time sources:
- Main lit pass viewport already uses `GetPresentProvider().GetExtent()`.
- Bloom dispatch uses the resolved HDR texture description.
- `WrapRenderPass` already derives its rendering area from the resolved attachments at record time.

- *Why not keep the params:* they are a second source of truth that is only correct for the first frame. All four callers pass the window/present size anyway, so dropping them changes nothing at startup.
- All four call sites are windowed (`example/external_resource_loading_example`, `test/project_loading_test`, `test/input_test`, `app/physics`); none is headless, so the headless hard-coded reference size (1920×1080) never interacts with this builder.

### D3. Aspect sync is a thin per-loop shim, not baked into `CameraManager`

`MainClass::RunOneFrame` and `PhysicsApp::RenderNextFrame` update the active camera's aspect from the present extent, only when the extent changes (`set_aspect_ratio` marks the projection dirty; `StartFrame → FetchCameraData → GetProjectionMatrix` recomputes lazily each frame, so no resize event is needed).

- *Why not `CameraManager`-side auto-sync:* aspect is a property of the *render target the camera draws into* (SceneWidget's camera follows the ImGui viewport, not the window). Baking "aspect == swapchain" into the camera system would fight the editor use case and cement the flawed camera API further.
- Marked interim (see Temporary section); a future camera redesign is expected to replace these four lines.

### D4. PhysicsApp mirrors the SceneWidget FOV behavior; the generic main loop only fixes aspect

- PhysicsApp stores a FOV baseline read from the camera's `m_fov_vertical` (default 45°) at creation. When the aspect crosses 1.0 it calls `set_fov_vertical(baseline)` (landscape) or `set_fov_horizontal(baseline)` (portrait), matching `SceneWidget::Render` — the reference the user asked to follow.
- MainClass `RunOneFrame` only calls `set_aspect_ratio`; a generic main loop must not silently change the application's FOV.

### D5. Two-layer 0×0 defense

- **Primary** — `RenderSystem::UpdateSwapchain`: return early when `w <= 0 || h <= 0`. The subsequent acquire fails and the existing skip-frame path handles it; on restore, `UpdateSwapchain` proceeds normally.
- **Secondary** — `ResizableRTTManager::SetReferenceSize`: ignore `0` width/height and keep the last valid reference size. Do **not** clamp to 1: a 1×1 texture is degenerate and would be blit-mismatched; ignoring preserves the last sane size and is the safer default. Document the ignore semantics on the method.

### D6. Bloom dispatch from the resolved texture, not the present extent

`cb.DispatchCompute(w / 16 + 1, h / 16 + 1, 1)` with `w/h` from `GetInternalTextureResource(hdr)->GetTextureDescription()`. Using the texture's own description keeps the dispatch self-consistent with what is bound (robust even if a future scale factor differs from 1.0).

## Risks / Trade-offs

- **VMA/texture churn on every resize** → `UpdateSwapchain` already `WaitForIdle`s before invalidating; re-creating three textures per resize is cheap relative to frame cost.
- **`ResizableRTTManager` global cache is invalidated on any resize** (all resizable RTTs) → this is by design (single global reference size); only the graph's three targets are resizable in this change.
- **Breaking API change (`BuildDefaultRenderGraph` signature)** → only four in-repo call sites; repo-local engine, no external consumers.
- **Resize ordering race (skip-frame vs rebuild)** → `StartFrame` retry + `UpdateSwapchain` + lazy resolve is already sequenced correctly by the frame state machine; no new synchronization added.
- **Camera polling could be mistaken for a new engine guarantee** → explicitly documented as interim in the Temporary section so it is not treated as stable contract.

## Known Temporary / To-be-replaced

- **Camera aspect sync (D3/D4)**: thin polling in `MainClass::RunOneFrame` and `PhysicsApp::RenderNextFrame`. Exists because the camera API lacks a size-notification mechanism. Expected to move/change when the camera system (`CameraManager` shared-ptr registration, `GetActiveCamera` → `shared_ptr<Camera>`, `display_id` routing) is redesigned. This change makes no camera API commitment.
- **Bloom (D6)**: placeholder single-pass implementation. Only the dispatch-count bug is fixed; the pass itself is expected to be rewritten.
- **PhysicsApp FOV switch (D4)**: copies `SceneWidget`'s heuristic; may converge with the editor once the camera/viewport model is unified.

## Migration Plan

- No data migration.
- Code-level ripples: update the four `BuildDefaultRenderGraph` call sites in the same commit as the signature change.
- Rollback: revert the change commit; the signature change is backward-incompatible only within this repo, which is updated atomically.

## Open Questions

- None blocking. The only behavioral judgment calls (FOV switch for PhysicsApp, aspect-only for the generic loop) were resolved with the user during grilling.
