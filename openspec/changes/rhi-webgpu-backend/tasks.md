## 1. Spikes (throwaway, findings feed the artifacts below)

- [ ] 1.1 Bake `blinn_phong` (texture + constant block), `fluid.comp` (shared memory + uint atomics + storage image) and `radix_scatter.comp` (shared memory + workgroup barrier) from SPIR-V through the candidate converter; record every fidelity problem in the change's spike notes and verify the three outputs are accepted by a WGSL validator
- [ ] 1.2 Verify the constant-block conversion path: convert a shader with a push-constant block and confirm the fallback (SPIR-V constant block rewritten to a uniform buffer before conversion) is needed or not; record the result in the spike notes
- [ ] 1.3 Measure one dispatch per compute scope vs batched dispatches in a minimal `webgpu.h` compute sample and record the overhead in the spike notes
- [ ] 1.4 Build a minimal `webgpu.h` triangle plus one compute dispatch on the Emscripten toolchain and verify it renders to a canvas and reads back a dispatch result
- [ ] 1.5 Record the target's limits (`maxBindGroups`, buffer and binding sizes) and the behavior of two dispatches sharing one compute scope in the spike notes

## 2. Build scaffolding

- [x] 2.1 Add the `ENGINE_RHI_BACKEND` configure variable accepting exactly `vulkan` and `webgpu`, and verify an unknown value fails configuration naming the accepted values
- [x] 2.2 Make the Vulkan SDK/glslang lookup and the Vulkan-only third-party targets conditional on the `vulkan` backend, and verify a `webgpu` configure succeeds on a machine without a Vulkan SDK
- [ ] 2.3 Add an Emscripten toolchain file and configure preset for the web build, and verify the preset produces a wasm library target
- [x] 2.4 Add the optional desktop WebGPU validation target linking the standard WebGPU C interface, and verify it builds a headless executable skeleton

## 3. Rhi interface purification

- [ ] 3.1 Introduce the portable value types (image format, extent, rectangle, sample count, usage bits) and the opaque handle/identity types, and verify every `engine/Rhi` public header compiles with no backend header on the include path
- [ ] 3.2 Replace backend types in `BindingRef`, `MemoryAllocation`, `DeviceInterface`, `AllocatorState` interfaces and verify the Vulkan build and its unit tests pass unchanged
- [ ] 3.3 Replace backend types in `DescriptorArena`, `ShaderResourceBinding`, `ImmutableResourceCache` and verify the descriptor tests pass unchanged
- [ ] 3.4 Introduce the command encoder with flat recording, `Barrier`, `TextureBarrier`, `SetKernelParams`, `SetUniformSlice` and the render scope, and verify the Vulkan implementation records exactly the commands it does today for an equivalent recording
- [ ] 3.5 Add the capability and limits query and verify a consumer can distinguish a single-queue backend and emulated constants without naming a backend
- [ ] 3.6 Verify a translation unit including every `engine/Rhi` public header links against neither Vulkan nor WebGPU headers

## 4. Physics recording migration

- [x] 4.1 Change the solver and detector interfaces from a raw command buffer to the command encoder, and verify the physics tests pass unchanged on the Vulkan backend
- [x] 4.2 Convert `DispatchBarrier` call sites to `encoder.Barrier()` at unchanged positions and verify the Vulkan command stream for one physics step is identical to before
- [x] 4.3 Route `ComputeKernel::Dispatch`'s constant argument through `SetKernelParams` and verify the reflected block-size assertion still fires on an oversized block
- [x] 4.4 Verify `gpu_algorithm` recordings contain no new barrier and no new dispatch relative to the current Vulkan stream

## 5. Render and framework migration

- [x] 5.1 Replace the raw command buffer in the render command recorder and the render graph's pass functions with the encoder, and verify golden images are unchanged
- [x] 5.2 Replace `vk::Extent2D`/`vk::Format`/semaphore types in the presentation provider, attachment descriptions and frame manager interfaces, and verify windowed and headless presentation still work
- [ ] 5.3 Route material per-frame uniform writes and physics host writes through the mapped-window contract, and verify the existing resource and submission tests pass
- [ ] 5.4 Verify `engine/Render`, `engine/Physics`, `engine/Framework` and `engine/Core` contain no backend type in any declaration

## 6. Mapped window

- [ ] 6.1 Implement the mapped-window contract on the Vulkan backend with the window permanently open, and verify pointer stability and flush behavior are unchanged
- [ ] 6.2 Implement dirty accounting and automatic flush-before-submission, and verify a caller that never calls `Flush` still has its writes visible to the consuming submission
- [ ] 6.3 Implement asynchronous readback registration and the blocking pump, and verify the existing headless tests pass through the pump with unchanged assertions
- [ ] 6.4 Verify product frame paths contain no blocking wait on device completion

## 7. Shader artifacts

- [ ] 7.1 Extend the shader baker with the WGSL variant and the interface description (binding table, variable offsets, constant-block metadata, combined-sampler split map) and verify a baked artifact loads on both runtimes from one asset set
- [ ] 7.2 Implement bake-time layout validation between the produced WGSL and the description, and verify a deliberately mismatching layout fails the bake naming the shader
- [ ] 7.3 Implement the constant-block rewrite fallback and verify a push-constant shader bakes to usable WGSL
- [ ] 7.4 Emit the same side artifacts for the physics shader tree and verify a physics kernel can be created without any reflection library linked
- [ ] 7.5 Verify the Vulkan runtime's loading and reflection behavior is unchanged with and without the WGSL variant present

## 8. WebGPU adapter

- [ ] 8.1 Implement device, queue, resource creation and capability query against the standard WebGPU C interface and verify the desktop validation target creates a device and reports its limits
- [ ] 8.2 Implement binding from the interface description with combined-sampler expansion and content-keyed bind-group caching, and verify identical requests reuse one bind group while distinct windows do not
- [ ] 8.3 Implement per-dispatch compute scopes with marks as no-ops and the rebind-on-scope-boundary behavior, and verify a marked and an unmarked recording both produce ordered results
- [ ] 8.4 Implement the constant ring and per-dispatch window binding, and verify a dispatched shader observes the recorded constant block
- [ ] 8.5 Implement mapped-window host access with queue-ordered uploads and completion-driven readback, and verify no mapped buffer is consumed by device work
- [ ] 8.6 Implement error and device-loss reporting through the logging path and verify a deliberately invalid recording is reported naming the operation

## 9. WebGPU compute path and cross-backend comparison

- [ ] 9.1 Run the physics compute pipeline on the WebGPU backend and verify its buffers match the Vulkan backend's for the same scene
- [ ] 9.2 Add headless golden-image comparison between the desktop WebGPU build and the Vulkan build and verify a fixed scene matches within the agreed tolerance
- [ ] 9.3 Verify the per-dispatch-scope overhead measured in spike 1.3 is within the recorded expectation for one physics step

## 10. Frame loop and presentation

- [ ] 10.1 Convert frame completion to callbacks advancing the epoch watermark and verify resource retirement still reclaims at the same watermark
- [ ] 10.2 Replace the blocking frame-slot wait with skip-and-retry and verify a busy slot skips the frame instead of blocking
- [ ] 10.3 Implement presentation through the host surface and verify the frame ring's per-frame windows stay distinct when the host exposes a single surface image
- [ ] 10.4 Port the UI backend to the WebGPU backend and verify the editor UI renders

## 11. Emscripten product build

- [ ] 11.1 Build the engine and an example for the browser and verify it renders and runs the physics loop at interactive rates
- [ ] 11.2 Verify the web build links neither the Vulkan SDK, VMA, glslang nor SPIRV-Cross
- [ ] 11.3 Move the test suite's synchronous readback onto the blocking pump with test-only yield support and verify the tests pass in the desktop WebGPU build

## 12. Documentation and closure

- [ ] 12.1 Write the portable-subset capability matrix (what the seam guarantees, what each backend maps, what is forbidden inside a render scope) into the change and verify it matches the implemented behavior
- [ ] 12.2 Record the spike findings and the chosen converter and desktop implementation as decisions in the change
- [ ] 12.3 Verify `openspec validate` passes and every requirement scenario in the delta specs has a corresponding test or documented verification


## Implementation status

Written at the end of the first implementation pass. Checkboxes above are
truthful: a task is checked only when its specified behaviour is in place and
verified. This section records what a later pass should pick up and in what
order.

### Done and verified

- **2.1** `ENGINE_RHI_BACKEND=vulkan|webgpu`, and an unknown value fails
  configuration with `ENGINE_RHI_BACKEND must be one of vulkan, webgpu (got
  '...')`.
- **2.2** The Vulkan SDK lookup, VMA, glslang, SPIRV-Cross and the ImGui Vulkan
  back-end are conditional on the backend. A `webgpu` configure was run with
  `VULKAN_SDK` emptied and completed without ever looking for Vulkan.
- **2.4** `webgpu-desktop` preset plus the `webgpu_spike` target: a headless
  executable that creates a device through the standard WebGPU C interface,
  renders a triangle, dispatches a kernel and reads both back. It runs to
  completion on wgpu-native.
- **Part of 1.1 - 1.3, 1.5**: the converter was measured against the real
  corpus, and every fidelity problem is recorded in `spike-notes.md` (F1 to F8)
  with the rewrite that addresses it. The scope-overhead measurement, the limits
  and the intra-scope ordering observation are recorded there too.

### Done but not verified

- **2.3** The Emscripten toolchain file (`cmake/toolchains/emscripten.cmake`),
  the `emscripten` preset and the SDL3 port module (`cmake/EmscriptenSdl3.cmake`)
  are written. The preset has never been configured, because no Emscripten
  toolchain could be installed on this machine.

### Blocked outside the change

The browser half of the spikes, and therefore group 11, needs an Emscripten
toolchain. Installing one means about a gigabyte over a link that measured at
9 KB/s; both `emsdk` and a Dawn clone were abandoned after repeated stalls. The
same limit ruled out building Tint to compare converters. Details are in
`spike-notes.md` under "Status of the two spikes".

### Not started

Groups 3 to 10 and 12. The portable types those groups need are in place
(`engine/Rhi/RhiTypes.h`: opaque typed handles, extents, rectangles, sample
count, descriptor kind and resource state), but no `Rhi` header has been
switched over to them yet, so the interface is still Vulkan-typed and the counts
in `design.md` still hold.

The order the tasks are written in is still the right order. The one change a
later pass should consider is pulling the shader bake (group 7) ahead of the
interface work: the bake is independent of the seam, the converter's failures
are the largest unknown left, and `tools/shader_baker/spirv_edit.*` and
`wgsl_bake.*` already reflect a module, rewrite its constant block into a
uniform buffer and emit the interface description, so continuing there is
cheaper than starting the seam and coming back.

### Progress after the second pass

- **7.4 (first half):** `shader_baker --spirv-dir` now bakes the *whole* physics
  corpus — 42 of 42 kernels produce a WGSL variant and an interface description
  that pass the converter's validator — and `physics_shader_wgsl` wires that into
  a WebGPU build. The second half of the task (a physics kernel created with no
  reflection library linked) still needs the WebGPU backend, so the task stays
  open.
- **7.3 (constant block):** implemented and exercised on every physics kernel
  that declares one; the reserved set is derived from the reflected sets, so a
  compute shader's block lands at `set 1, binding 0` and a material shader's at
  `set 3, binding 0`.
- **New finding F9 (spike notes):** a WebGPU build cannot compile the `Rhi`
  module at all yet, because `PipelineEnums.h` includes `<vulkan/vulkan.hpp>` for
  one conversion function. Group 3 is therefore the gate on every group after it,
  not just a tidy-up.
- **7.1 / 7.2 (converter half):** Tint is the bake's default converter, and 40 of
  the 42 physics kernels bake to WGSL plus a description. The two remaining
  failures are one source-level defect (F11, a workgroup barrier under non-uniform
  control flow), not a bake defect. The binding-table half of 7.2 is written
  (the description is generated from the same reflection the Vulkan path uses);
  the deliberate-mismatch test it asks for is not.
- **Group 3 has started:** `Rhi/Pipeline/PipelineEnums.h` no longer includes a
  backend header, and the WebGPU build now fails on `Rhi/Device/BindingRef.h`
  instead — the compiler is walking the list in dependency order.
### Progress after the third pass

- **3.4 (interface half):** `Rhi/Command/CommandEncoder.h` exists, with the
  Vulkan implementation recording exactly the compute-to-compute memory barrier
  the physics step's hand-written `DispatchBarrier` recorded, so the encoder's
  first backend reproduces today's command stream byte for byte. The encoder is
  not yet *used*: `ComputeKernel::Dispatch` and the solvers still take a raw
  command buffer, which is task 4.1.
- **3.2 done for four headers:** `BindingRef`, `AllocatorState`,
  `DeviceInterface` (with `DeviceContext`), and `PipelineEnums` carry no backend
  type. The conversions live in `Rhi/Backend/Vulkan/VulkanHandles.h` and
  `ImageUtilsFunc.h`. `DeviceConfiguration::dynamic_dispatcher` was deleted: every
  caller passed nullptr and it was the last Vulkan-Hpp type in a portable header.
- **Remaining in 3.2/3.3:** `Structs.h` (`QueueInfo` owns Vulkan queues and
  command pools, `SwapchainSupport` owns Vulkan structures), `MemoryAllocation.h`
  (VMA handles and `vk::Image`/`vk::Buffer`), `DebugUtils.h`,
  `ImmutableResourceCache.h`, `ShaderResourceBinding.h`, `InterfaceBindingResolver.h`,
  `ShaderParameterLayout.h`, `ComputeKernel.h`, `Texture.h`, `EpochTracker.h`,
  `SubmissionHelper.h`. `QueueInfo` is the one that does not have a mechanical
  answer: its members are backend objects, and they stop being reachable from
  consumers when 4.1/5.1 route recording through the encoder, so the encoder
  migration should come before the queue/pool accessors are redesigned.
### Attempt at 4.1-4.3, and why it was rolled back

The physics migration was carried out and then reverted rather than committed
half-done. What worked, and is worth keeping for the next attempt:

- `PhysicsDispatch.h` becomes two lines — `DispatchBarrier(encoder)` calls
  `encoder.Barrier()` — which keeps all 35 call sites' *positions* unchanged and
  is what preserves the Vulkan command stream.
- Every physics header and source compiles with `vk::CommandBuffer cb` replaced
  by `Rhi::CommandEncoder &encoder`: 20 files, and the bodies needed only
  `DispatchBarrier(cb)` -> `DispatchBarrier(encoder)` and
  `->Dispatch(cb,` -> `->Dispatch(encoder,`. `EnginePhysics` built clean.
- `ComputeKernel::Dispatch`/`DispatchAndRecord` take the encoder and bind the
  Vulkan command buffer once, from `encoder.GetHandle()`.

What failed: the same substitution applied to the *test* surface by regular
expression. The tests declare their command buffer in several different ways
(`auto cb = f.Begin()`, a `CommandBuffer &` parameter, a `vk::CommandBuffer`
built by hand), so a regex that inserts an encoder after every `cb` declaration
produced duplicated declarations and encoders built from the wrong object. The
fix is not a better regex: each test needs its encoder declared where its
command buffer is, which is a file-by-file pass of about a dozen files.

So 4.1-4.3 stay open, with the shape above already proven on the engine side.
The next attempt should migrate `engine/Physics` first (it compiles), then walk
`test/` file by file, and only then delete `PhysicsDispatch.h`'s Vulkan include.
### Progress after the fourth pass

- **4.1, 4.2, 4.3, 4.4 done.** The solver, detector and `gpu_algorithm`
  interfaces take `Rhi::CommandEncoder`; `DispatchBarrier` is the seam's mark at
  all 35 of its call sites, unchanged in position; `ComputeKernel::Dispatch`
  records through `BindComputePipeline` / `BindDescriptorSet` /
  `SetKernelParams` / `DispatchCompute` instead of calling Vulkan directly, so
  the constant block now travels as kernel parameters. The oversized-block
  assertion still fires (`compute_kernel_test`'s `TooLarge` case), no recording
  gained a barrier or a dispatch, and all 77 tests pass — which is the evidence
  that the Vulkan command stream is unchanged.

  One caveat worth stating: the *assertions* in the tests are unchanged, but the
  test sources themselves were edited where they call `Record`/`Dispatch`, to
  open an encoder where their command buffer is created. That is plumbing, not
  behaviour, but it means "tests pass unchanged" is true of the checks rather
  than of the files.

### Progress after the fifth pass

`engine/Rhi` public headers that still carry a backend type, and what each needs:

| Header | What is left |
| --- | --- |
| `ImmutableResourceCache.h` | `vk::Device` ctor, `vk::Sampler`/`vk::DescriptorSetLayout`/`vk::PipelineLayout` returns, and `vk::*CreateInfo` parameters |
| `DescriptorArena.h` | `vk::DescriptorType`, `vk::ImageLayout`, `vk::DescriptorSet` returns and `vk::DescriptorSetLayoutCreateInfo` |
| `Texture.h` | `vk::Image`/`vk::Sampler`/`vk::ImageView` getters (callers are Render and Physics) |
| `ComputeKernel.h` | `GetPipeline`/`GetPipelineLayout`/`GetDescriptorSetLayout` returns |
| `EpochTracker.h`, `SubmissionHelper.h`, `TextureSubresourceView.h`, `InterfaceBindingResolver.h`, `ShaderParameterLayout.h`, `DeviceBuffer.h`, `ShaderResourceBinding.h`, `ShaderInterface.h` | one to two `vk::` each |

Four headers left the portable set this pass by moving into `Rhi/Backend/Vulkan`:
`VulkanImageUtils.h`, `VulkanStructs.h`, `VulkanDebugUtils.h`, plus the
`MemoryAllocation.h` rewrite.

The remainder is no longer a mechanical sweep: each entry is a *return type*
whose callers are in Render or Physics, so it belongs with 5.1/5.2 rather than
before them. Converting those getters earlier means converting Render twice.


### 5.1 note

The render-side `CommandBuffer` now owns a `Rhi::CommandEncoder` over the same
recording and hands it out through `GetEncoder()`, so a render graph pass can
record compute work and synchronization through the seam without naming a
backend. Every pass that had grown a hand-built encoder in the previous pass now
uses `cb.GetEncoder()`, which removed the last `FromVulkan` call from the
recording call sites.

The raw `GetCommandBuffer()` accessor is still there: the transfer and rendering
methods inside `CommandBuffer` itself are Vulkan-shaped and have not been
migrated, so 5.2 (attachment and presentation types) is what removes it.

### Attempt at 5.2, and why it was rolled back

Changing `BeginRendering`/`SetupViewport`/`DrawRenderers` from `vk::Extent2D`
and `vk::Rect2D` to `Rhi::Extent2D` and `Rhi::Rect2D` is the right move and the
four signatures convert cleanly. What does not convert cleanly is the call
surface: `Extent2D` and `Rect2D` are names that *both* the seam and Vulkan
declare, and the same translation units use both. Inside `CommandBuffer.cpp` the
portable extent feeds a `vk::RenderingInfo{...}` and a
`vk::CommandBuffer::setScissor`, which need the Vulkan `Rect2D`; a token-level
replacement across the tree rewrote those too and broke the file in a way that
looks like a type error but is really a scope error.

So 5.2 needs per-call-site edits with an explicit conversion at each boundary
(`vk::Rect2D{{0, 0}, {extent.width, extent.height}}` where a Vulkan structure is
built, and `Rhi::Extent2D{...}` where the portable one is passed), not a sweep.
The signatures themselves are known-good: they were the first thing that
compiled before the call surface was touched.

This also reprioritises the round: the same `Extent2D`/`Format` collision exists
in `IPresentProvider` and `AttachmentUtilsFunc`, so 5.2 should land provider by
provider, starting with the presentation interface where the types are fewest.
### 5.2, first provider landed

`IPresentProvider::GetExtent`, `GetColorFormat` and `Recreate` now speak
`Rhi::Extent2D` and `Rhi::ImageFormat`, and so do both implementations and the
`SceneDataManager::DrawSkybox` call that takes an extent. The render command
buffer still takes `vk::Extent2D` for `BeginRendering`/`DrawRenderers`; the
conversion happens at the call site with an explicit
`vk::Extent2D{extent.width, extent.height}`, which is the shape the rest of 5.2
should take.

Done per call site, as the rollback note predicted: thirty-odd edits, each one
either "this argument is portable" or "this argument builds a Vulkan structure",
and the compiler named every one. All 77 tests pass.
### 5.2, second step: the render recording surface speaks portable extents

`CommandBuffer::BeginRendering` (both overloads), `DrawRenderers` (both),
`SetupViewport` and `GUISystem::DrawGUI` now take `Rhi::Extent2D` and
`Rhi::Rect2D`. The seam-to-Vulkan conversions happen inside the recording
implementation, where the `vk::RenderingInfo` and `setScissor` calls build their
own structures from the portable values.

This is the pattern the previous note described, applied at the boundary the
other way round: the interfaces are portable, so every call site that used to
construct a Vulkan extent into a portable parameter now passes the portable one
straight through, and only the implementation converts. All 77 tests pass.


### 5.2 complete

`IPresentProvider` now speaks the seam end to end: `GetExtent`/`Recreate` take
`Rhi::Extent2D`, `GetColorFormat` returns `Rhi::ImageFormat`, `AcquireNextImage`
takes a `DeviceHandle` and a `SemaphoreHandle`, `PrepareCopy` takes a
`DeviceHandle` and returns a `CommandBufferHandle`, and `Present` takes both
handles. `RhiTypes.h` gained `SemaphoreHandle` and `FenceHandle` with their
Vulkan conversions for that.

Both providers, `FrameManager` (the acquire, copy-batch and present sites) and
the test's controllable provider convert at their own edge; the frame-completion
batch still submits through Vulkan because submission itself is 6.x, not 5.2.
All 77 tests pass.
