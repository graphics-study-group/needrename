# Proposal: RHI backend-neutral interface and WebGPU backend

## Why

`Rhi` is a Vulkan-shaped layer: `vk::` and VMA types cross its public interfaces, and `vk::CommandBuffer` leaks far beyond it — 105 of 280 engine sources name Vulkan types, 66 of them outside `engine/Rhi` (Render 42, Physics 20, Framework 3, Core 1). Adding a second backend today would mean rewriting the seam rather than adding an adapter.

At the same time the engine already draws on the WebGPU-shaped subset of Vulkan: Vulkan 1.3 dynamic rendering and synchronization2, timeline semaphores, no bindless or descriptor indexing, no geometry/tessellation, uint32-only shader atomics, single-threaded CPU side. A portable baseline therefore costs interface work, not feature cuts.

The goal is a WebGPU backend whose product form is the browser (Emscripten), with a desktop WebGPU build kept strictly as a validation and A/B environment.

## What Changes

- **BREAKING** `Rhi` public headers become backend-neutral. No `vk::`, VMA, or other backend type appears in any `Rhi` header or in any consumer signature (`ISolver::GPUStep`, `CommandBuffer`, `IPresentProvider`, `AttachmentUtils`). Backend types live only inside one backend's implementation units.
- The backend is chosen at configure time (`ENGINE_RHI_BACKEND=vulkan|webgpu`); exactly one backend is compiled into a build. A `webgpu` build carries neither Vulkan SDK, VMA, glslang nor SPIRV-Cross.
- A command-encoder contract replaces raw command-buffer recording: flat recording of dispatch / draw / copy with explicit synchronization marks (`Barrier`, `TextureBarrier`) that the Vulkan backend turns into barriers and the WebGPU backend turns into pass splits. `compute pass` is an implementation detail of the WebGPU backend and does not appear in the interface.
- Per-dispatch constants travel through `SetKernelParams` (semantics: kernel parameters, like CUDA kernel arguments). Vulkan records real push constants when the block fits; WebGPU records a ring-UBO slice with a cached bind group. Callers are unchanged.
- Per-draw/per-dispatch uniform windows travel through `SetUniformSlice(resource, offset, size)`. Vulkan keeps dynamic offsets; WebGPU caches one bind group per (resource, offset, size) content key.
- CPU access to GPU memory is redefined as a mapped window: `GetVMAddress()` returns the current window pointer, `Flush()` closes the window and makes writes visible to the device, `InvalidateMemory()` refreshes the window. The Vulkan backend keeps the window permanently open (behavior unchanged); the WebGPU backend implements it as a map/unmap state machine with dirty-range accounting.
- Shader artifacts gain a WGSL variant baked at build time, stored side by side with the existing SPIR-V form together with an interface description (binding table, variable offsets, push-block metadata, combined-sampler split map). The Vulkan runtime keeps reading SPIR-V and reflecting as today; the WebGPU runtime loads the description and never reflects.
- A WebGPU backend implements the contract: bind groups derived from the interface description and cached by the existing descriptor-arena content keying, one compute pass per dispatch with synchronization marks as no-ops, kernel-params ring buffer, mapped-window readback, surface/canvas presentation, and callback-based frame completion.
- Frame synchronization loses its blocking waits. Fence/semaphore waits become completion callbacks that advance the epoch watermark; a frame slot that is still busy is skipped rather than waited on. Synchronous readback survives only as a device-event pump used by tests and load-time code.
- Two new build shapes: an Emscripten web build (product) and an optional desktop WebGPU build (validation). Golden-image A/B between backends runs on the desktop WebGPU build; the browser remains the behavioral reference.

Not changed: Vulkan behavior — barrier placement, push constants, dynamic offsets, VMA allocation, queue usage, RenderGraph barrier generation — stays as it is today, so the Vulkan path is the regression baseline for the whole migration.

## Capabilities

### New Capabilities

- `rhi-backend-selection`: the `Rhi` layer is backend-neutral and selects exactly one backend at configure time; public headers expose only portable value types and opaque handles, and a capability/limits query replaces backend-specific feature checks.
- `rhi-command-encoder`: the flat recording contract — dispatch, draw and copy recorded without pass vocabulary; explicit synchronization marks whose meaning is "synchronize here"; kernel parameters and uniform slices as recording operations; the render scope as the only pass-shaped construct in the interface.
- `rhi-mapped-window`: the CPU-access contract for GPU memory — mapped windows for host writes and device readback, dirty accounting, flush/invalidate semantics, and asynchronous readback with a blocking pump reserved for tests and load-time code.
- `shader-wgsl-artifact`: shader artifacts carry a baked WGSL variant and an interface description alongside the existing SPIR-V form; the description is the single source of CPU-side layout truth for the WebGPU runtime, and the bake validates layout agreement.
- `webgpu-backend`: the WebGPU backend implementation and its two build shapes — bind-group binding from the interface description, per-dispatch compute passes, no-op synchronization marks, mapped-window readback, presentation, callback-based completion, plus the Emscripten product build and the desktop validation build.

### Modified Capabilities

- `rhi-compute-kernel`: dispatch records kernel parameters rather than Vulkan push constants directly; the reflected block becomes the size contract of `SetKernelParams`, and each backend maps it to its own mechanism. Barrier freedom of the kernel is unchanged.
- `submission-helper-sync`: the submission completion signal becomes backend-neutral (no `vk::SemaphoreSignalInfo` in the interface), and staging uploads consume the mapped-window contract instead of a permanently mapped VMA allocation.

## Impact

- `engine/Rhi/**`: every public header loses `vk::`/VMA types; `BindingRef`, `MemoryAllocation`, `DeviceInterface`, `ShaderResourceBinding`, `DescriptorArena`, `ComputeKernel`, `SubmissionHelper`, `EpochTracker` gain portable signatures; a backend implementation directory is added per backend.
- `engine/Render/**` (42 files) and `engine/Physics/**` (20 files): `vk::CommandBuffer` parameters become `Rhi::CommandEncoder &`; `IPresentProvider`, `AttachmentUtils`, `CommandBuffer` drop `vk::Extent2D`/`vk::Format`/`vk::Semaphore`; `DispatchBarrier` becomes `encoder.Barrier()` at unchanged call sites.
- `engine/Physics/gpu_algorithm`, solvers and detectors record through the encoder; their shader sources keep `layout(push_constant)` blocks.
- `tools/shader_baker`: gains a WGSL + interface-description bake stage (SPIR-V to WGSL through an external converter) and a layout validation step.
- `engine/Render/Asset/Shader` and `engine/Physics` shader trees: gain side-by-side `.wgsl` and layout artifacts.
- Build system: `ENGINE_RHI_BACKEND` option, conditional Vulkan toolchain lookup, Emscripten toolchain file and preset, optional desktop WebGPU link against `webgpu.h`.
- Tests: `test/unit/rhi`, `test/engine/headless` move synchronous readback to the mapped-window pump; a shader-artifact test covers the WGSL bake; a WebGPU golden-image comparison runs against the desktop WebGPU build.
- Dependencies added: an SPIR-V to WGSL converter at bake time (host-side only), and either wgpu-native or Dawn for the desktop validation build. Dependencies excluded from the web build: Vulkan SDK, VMA, glslang, SPIRV-Cross.
