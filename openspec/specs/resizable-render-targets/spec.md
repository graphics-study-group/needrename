# resizable-render-targets

## Purpose

Make render target textures and the present path follow the runtime swapchain size, so the presented image always covers the window correctly across resizes without rebuilding the render graph, and keep degenerate zero-size windows handled safely.

## Requirements

### Requirement: Presentable frame follows the swapchain size

The default render graph (`ComplexRenderGraphBuilder`) SHALL size its Final Color, HDR Color, and Depth render target textures to the current swapchain size (scale 1.0 resizable render targets), so the present blit is always 1:1 with the swapchain. The window's default size SHALL still be controlled by the caller-provided initialization parameters.

#### Scenario: Window enlarged beyond the initial size

- **WHEN** the runtime window is resized larger than its initial size and the swapchain is recreated
- **THEN** the presented image covers the entire window with correct scaling and SHALL NOT show clamped edge-color smearing

#### Scenario: Window shrunk below the initial size

- **WHEN** the runtime window is resized smaller than its initial size and the swapchain is recreated
- **THEN** the presented image covers the entire window with correct scaling and SHALL NOT be cropped or compressed

#### Scenario: Window restored from minimize

- **WHEN** a minimized window is restored to a non-zero size
- **THEN** the render target textures are recreated at the restored size and rendering continues at the restored size

### Requirement: Default graph derives render-time sizes at record time

`ComplexRenderGraphBuilder::BuildDefaultRenderGraph` SHALL NOT accept texture width/height arguments. Render-time dimensions (raster viewport, rendering area, compute dispatch) SHALL be derived at record time from the currently-resolved textures or the present provider extent, so they remain correct across resizes without rebuilding the graph.

#### Scenario: Resize requires no graph rebuild

- **WHEN** the swapchain size changes while the compiled render graph stays alive
- **THEN** every recorded pass uses dimensions matching the new texture sizes with no graph rebuild and no stale build-time size

### Requirement: Bloom dispatch covers the full HDR texture

The bloom compute pass SHALL dispatch enough workgroups to cover the entire resolved HDR texture on every frame, with dispatch dimensions derived at record time.

#### Scenario: HDR texture grows after enlarge

- **WHEN** the window/HDR texture is enlarged such that the old dispatch grid would not cover it
- **THEN** the bloom pass dispatches a grid covering the full new HDR texture, leaving no unwritten texels in the presented image

#### Scenario: HDR texture shrinks after shrink

- **WHEN** the window/HDR texture is shrunk such that the old dispatch grid would exceed it
- **THEN** the bloom pass dispatches a grid matching the new HDR texture size, with no out-of-bounds image stores

### Requirement: Degenerate zero-size window is handled safely

`RenderSystem::UpdateSwapchain` SHALL skip swapchain and resizable render target recreation when the window pixel size is zero, keeping the previous extent. `ResizableRTTManager::SetReferenceSize` SHALL ignore a zero width or height and keep the last valid reference size.

#### Scenario: Window minimized

- **WHEN** the window is minimized and reports a zero pixel size during swapchain recreation
- **THEN** recreation is skipped, no assertion or crash occurs, and the next scheduled frame is skipped until the window is restored

### Requirement: Active camera aspect ratio follows the present extent

The runtime loop(s) that drive a window SHALL update the active camera's aspect ratio to match the current present extent whenever the extent changes, before the next frame is submitted.

#### Scenario: Window aspect changes

- **WHEN** the present extent's width/height ratio changes (including landscape/portrait crossing)
- **THEN** the active camera's aspect ratio is updated so the rendered scene is not stretched, deformed, or over-zoomed

### Requirement: PhysicsApp window is resizable with a stable camera

The `PhysicsApp` window SHALL be resizable, and on resize the presented image SHALL cover the window without clamp, stretch, or crop. When the aspect ratio crosses 1.0, the PhysicsApp camera SHALL switch between a fixed vertical FOV (landscape) and a fixed horizontal FOV (portrait) to keep a stable view.

#### Scenario: PhysicsApp window enlarged

- **WHEN** the PhysicsApp window is dragged larger than its initial size
- **THEN** the scene fills the whole window with no edge-color clamping

#### Scenario: PhysicsApp window in portrait orientation

- **WHEN** the PhysicsApp window aspect ratio drops below 1.0
- **THEN** the camera uses a fixed horizontal FOV so the scene's visible extent is stable and not over-zoomed

### Requirement: Generic main loop updates aspect only

`MainClass::RunOneFrame` SHALL update the active camera's aspect ratio to match the present extent but SHALL NOT alter the camera's field of view.

#### Scenario: Generic main-loop app resized

- **WHEN** an app using `MainClass::RunOneFrame` resizes its window
- **THEN** the presented image does not deform and the camera's FOV is unchanged
