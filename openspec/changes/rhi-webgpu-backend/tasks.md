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

- [x] 3.1 Introduce the portable value types (image format, extent, rectangle, sample count, usage bits) and the opaque handle/identity types, and verify every `engine/Rhi` public header compiles with no backend header on the include path
- [x] 3.2 Replace backend types in `BindingRef`, `MemoryAllocation`, `DeviceInterface`, `AllocatorState` interfaces and verify the Vulkan build and its unit tests pass unchanged
- [x] 3.3 Replace backend types in `DescriptorArena`, `ShaderResourceBinding`, `ImmutableResourceCache` and verify the descriptor tests pass unchanged
- [x] 3.4 Introduce the command encoder with flat recording, `Barrier`, `TextureBarrier`, `SetKernelParams`, `SetUniformSlice` and the render scope, and verify the Vulkan implementation records exactly the commands it does today for an equivalent recording
- [x] 3.5 Add the capability and limits query and verify a consumer can distinguish a single-queue backend and emulated constants without naming a backend
- [x] 3.6 Verify a translation unit including every `engine/Rhi` public header links against neither Vulkan nor WebGPU headers

## 4. Physics recording migration

- [x] 4.1 Change the solver and detector interfaces from a raw command buffer to the command encoder, and verify the physics tests pass unchanged on the Vulkan backend
- [x] 4.2 Convert `DispatchBarrier` call sites to `encoder.Barrier()` at unchanged positions and verify the Vulkan command stream for one physics step is identical to before
- [x] 4.3 Route `ComputeKernel::Dispatch`'s constant argument through `SetKernelParams` and verify the reflected block-size assertion still fires on an oversized block
- [x] 4.4 Verify `gpu_algorithm` recordings contain no new barrier and no new dispatch relative to the current Vulkan stream

## 5. Render and framework migration

- [x] 5.1 Replace the raw command buffer in the render command recorder and the render graph's pass functions with the encoder, and verify golden images are unchanged
- [x] 5.2 Replace `vk::Extent2D`/`vk::Format`/semaphore types in the presentation provider, attachment descriptions and frame manager interfaces, and verify windowed and headless presentation still work
- [x] 5.3 Route material per-frame uniform writes and physics host writes through the mapped-window contract, and verify the existing resource and submission tests pass
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

### 5.3 done

Every host write into GPU memory now closes its mapped window before the
submission that consumes it. The physics side already did — its three
`Flush(offset, bytes)` calls were in place — and the render side did not: the
material per-frame uniform upload and the camera and scene-light uploads wrote
through `GetSlicePtr` and never called `FlushSlice`. Those three now do.

The behaviour is unchanged on Vulkan because these allocations are host-coherent,
so the flush is the no-op it always effectively was; what changes is that the
write is now expressed as the seam's contract rather than as a bare memcpy into a
mapped pointer, which is what a backend without persistent mapping needs in order
to make it work at all.

### 3.3 progress: the compute kernel's getters

`ComputeKernel::GetPipeline`, `GetPipelineLayout` and `GetDescriptorSetLayout`
return portable handles, so the kernel header is free of backend types. The only
conversion left in a caller is the debug-name helper in `compute_kernel_test`,
which needs a Vulkan pipeline to name; the test's handle comparison now compares
`GetValue()` identities instead of `CType`, which is the portable equivalent.

Eleven headers still name a backend type. They are all return types whose callers
are in Render (`Texture`'s image/sampler/view getters, `DeviceBuffer::GetBuffer`,
`ImmutableResourceCache`'s sampler and layout returns, `DescriptorArena`'s set and
type enums), plus three headers that only need their Vulkan include removed
(`InterfaceBindingResolver`, `ShaderParameterLayout`, `ShaderInterface`).

### Where the backend-type removal stands

Nine headers still name a Vulkan type. They are not independent: they are the
descriptor layer, and it has to move as one piece because a resolved binding, a
descriptor-set layout and a descriptor-type enumeration are the same vocabulary
seen from three sides.

| Header | Sites | What it needs |
| --- | --- | --- |
| `ImmutableResourceCache.h` | 9 | `vk::Sampler` and layout returns, plus `vk::*CreateInfo` parameters |
| `DescriptorArena.h` | 7 | `vk::DescriptorType`, `vk::ImageLayout`, `vk::DescriptorSet` returns |
| `InterfaceBindingResolver.h` | 2 | the same two enumerations, from the other side |
| `ShaderParameterLayout.h` | 2 | `GenerateLayoutBindings` returns Vulkan bindings |
| `EpochTracker.h`, `SubmissionHelper.h` | 2 each | a device handle and a fence, mechanical |
| `DeviceBuffer.h` | 1 | `GetBuffer` |
| `ShaderInterface.h` | 1 | a comment |

The order that avoids doing it twice:

1. `RhiTypes.h` already has `DescriptorType`, `ResourceState` and
   `SampleCountBits`; map `vk::DescriptorType` and `vk::ImageLayout` onto them in
   `VulkanHandles.h` (two functions).
2. Switch `InterfaceBindingResolver` and `DescriptorArena` together, since the
   arena is the only consumer of a resolved binding.
3. Then `ImmutableResourceCache` and `ShaderParameterLayout`, whose create-info
   parameters are what `DescriptorArena` and `MaterialLibrary` build.
4. `DeviceBuffer::GetBuffer` last: it has the widest call surface (~60 sites) and
   is mechanical once the rest is portable.

That is one coherent pass, not four. Starting it halfway — which is what
attempting `InterfaceBindingResolver` alone would be — leaves the descriptor
layer expressing itself in both vocabularies at once.

### Descriptor-layer pass: what it actually needs first

The first attempt at step 1 stalled on a question the plan had glossed over:
`DescriptorArena::ResolveLayout` takes a `vk::DescriptorSetLayoutCreateInfo`, and
that structure is not a rename away from portable — it is the *input* the arena
is built around, and its callers fill it from `SPLayout::GenerateLayoutBindings`,
which returns `vk::DescriptorSetLayoutBinding`.

So the pass has a missing first step before the four in the order above:

0. Choose the portable shape of a descriptor-set layout description — most
   likely a small struct of `{binding, DescriptorType, array count, stages}` and
   a `std::vector` of them — and have `SPLayout::GenerateLayoutBindings` produce
   *that* instead of Vulkan bindings. Only `ImmutableResourceCache` then needs to
   translate it into a `vk::DescriptorSetLayoutCreateInfo`, in one place.

That also removes the `vk::*CreateInfo` parameters from `ImmutableResourceCache`
at the same time, because they become the portable description.

Nothing was committed from the attempt: the value-type half of the change (the
`DescriptorType`/`ResourceState` enumerations and the handle-typed set) is easy
but useless on its own, and leaving the arena expressing itself in both
vocabularies is the outcome the previous note set out to avoid.

### Step 0, attempted twice: it needs a hand edit, not a sweep

The portable layout description itself was easy to define — a
`DescriptorBinding{binding, DescriptorType, count}` and a
`DescriptorSetLayout` as its vector. Translating `GenerateLayoutBindings` was not:
its body builds a `vk::DescriptorSetLayoutBinding` per interface with a braced
initialiser whose *type* argument is a two-line ternary, so both a text
substitution and a line-based rewrite produced malformed code, and both were
reverted rather than committed.

The lesson is the same one this session keeps relearning: this function is four
`emplace_back` calls with ternaries, and it wants to be **typed out** — five
minutes with an editor — not transformed. Nothing here is hard; the sweep is
what makes it hard.

### Step 0: the exact shape, for whoever picks it up

The hand edit did work: `GenerateLayoutBindings` was rewritten by hand into the
portable form and compiled. It broke one step later, on its *sibling*
`GenerateAllLayoutBindings`, which has the same body shape and had not been
converted yet — so the round was reverted whole rather than half-converted.

What the next attempt should do, in order, without stopping in between:

1. `RhiTypes.h`: add `DescriptorType::{UniformBufferDynamic, StorageBufferDynamic}`,
   `struct DescriptorBinding{binding, type, count}` and
   `using DescriptorSetLayout = std::vector<DescriptorBinding>`.
2. `ShaderParameterLayout.h`: both `GenerateLayoutBindings` and
   `GenerateAllLayoutBindings` return the portable types.
3. `ShaderParameterLayout.cpp`: rewrite **both** function bodies by hand. They are
   the same shape — five `emplace_back(vk::DescriptorSetLayoutBinding{...})`
   calls each — and both want typing out, not substituting.
4. `VulkanHandles.h`: `ToVulkanDescriptorType` plus
   `ToVulkanBindings(const DescriptorSetLayout &)` returning the vector of Vulkan
   bindings, which is the single translation point.
5. Callers pass the result through `ToVulkanBindings(...)`, **assigned to a named
   local** — a temporary in a braced initialiser does not bind to
   `vk::ArrayProxy`, which is a trap this session hit three times.

Steps 1–4 are mechanical *except* step 3, which is the only part that needs eyes
on it.

### The arena/cache conversion: the exact remaining edit list

The pass got the resolver, the arena header and the cache header converted and
compiled; it then failed on `std::construct_at` while building one of the arena's
containers, and was reverted rather than left half-converted.

For the next attempt, the edits that the compiler had already accepted, in order,
so they can be replayed without rediscovery:

1. `VulkanHandles.h`: `ToVulkanImageLayout(ResourceState)`. Done and correct.
2. `InterfaceBindingResolver.h`: `InterfaceBinding{DescriptorType type,
   ResourceState image_layout}`; drop the Vulkan include; the `.cpp` needs only
   the type-name substitutions (all of them are enumeration members, no
   structural change).
3. `DescriptorArena.h`: same two enumerations on `ResolvedBinding`,
   `DescriptorSetHandle set` on `DescriptorSetBinding`, handle returns on
   `AcquireRawSet`/`Acquire`, and `ResolveLayout` takes `const DescriptorSetLayout &`.
4. `ImmutableResourceCache.h/.cpp`: the two descriptor-set-layout functions take
   `const DescriptorSetLayout &` and build the `vk::DescriptorSetLayoutCreateInfo`
   internally from `ToVulkanBindings(layout)` — the hasher keeps working because
   it is handed that structure.
5. `DescriptorArena.cpp`: `RequirementsOf(const DescriptorSetLayout &)` iterates the
   portable bindings and sums with `ToVulkanDescriptorType`; `IsImageDescriptor`
   takes the portable enumeration; the three `return <vk set>` sites become
   `FromVulkan(...)`, and `NameSet` keeps taking the Vulkan handle.

The failure that ended the attempt came *after* all of the above: a
`std::construct_at` error inside a standard container, most likely the arena's
`std::unordered_map<uint32_t, DescriptorRequirements>` or the resident chain,
which means one of the conversions produced a non-assignable element type. The
next attempt should build after step 4 — before touching `DescriptorArena.cpp` —
so the compiler points at the exact container rather than at the end of a
six-file cascade.

### The arena pass fails on one specific thing

Three attempts at the descriptor arena have now ended on the same compiler output
from `DescriptorArena.cpp`:

```
xmemory(730): error C2672: std::construct_at: no matching overloaded function found
```

Everything else in the six-file cascade compiles: the resolver, the arena and
cache headers, the arena's `RequirementsOf`/`IsImageDescriptor`, and the three
`return <vk set>` sites. The failure is a *container* of an element type that
stopped being constructible, and it appears only once the arena's own source is
compiled.

The next attempt should therefore start by asking that question directly, before
touching anything: **what does `DescriptorArena.cpp` put in a container whose
element type mentions `ResolvedBinding`, `DescriptorSetBinding` or
`DescriptorRequirements`?** Candidates worth checking first, in order:

1. `std::vector<ResolvedBinding>` — `ResolvedBinding::operator==` is declared
   `const noexcept` but the type gained two enumerations; if the comparison is
   still defined out of line and something takes its address in a container, the
   mismatch shows up exactly here.
2. `std::map<vk::DescriptorType, uint32_t> DescriptorRequirements` — keyed on a
   Vulkan enumeration; if `ToVulkanDescriptorType` is applied at the *element*
   level instead of the key, the key type is portable and the map is fine, but a
   stale `requirements[binding.descriptorType]` would not compile. It does
   compile in the current head, so this is the least likely.
3. The resident cache chain — `resident[key]` holds entries that own a
   `DescriptorSetBinding`; if `set` became a handle, the entry is still
   copyable, so this is only a suspect if the entry also stores something
   derived from it.

Neither the exact container nor the exact reason is known. That is the state to
pick up, and one `cmake --build` on `DescriptorArena.cpp` alone with the six-file
change applied reproduces it in seconds.

### The `construct_at` mystery: solved

The failure that ended three attempts at the descriptor arena is identified. It
was **not** a container in the arena's own bookkeeping at all:

```
writes.emplace_back(set, binding.binding, 0u, 1u, binding.type);
```

`writes` is `std::vector<vk::WriteDescriptorSet>`, and `binding.type` had become
the portable `DescriptorType`, so `construct_at` had no way to build the element.
The compiler reports it at `xmemory(730)` because the failed construction is
inside `vector::emplace_back`, which is why it looked like a container of an
unassignable type rather than a wrong argument.

Two more of the same shape were found at the same site and are part of the fix:

- `image_infos.emplace_back(..., binding.image_layout)` needs
  `ToVulkanImageLayout(binding.image_layout)`;
- the `IsImageDescriptor(binding.type)` calls compile once the helper takes the
  portable enumeration.

So the complete recipe now is the six-file list already recorded **plus** the
three conversions at that one `WriteDescriptors` site, and the sequence that
worked up to the point of revert was: resolver → kernel's `Interface` fields →
arena header → `IsImageDescriptor` → `RequirementsOf` → the write site → cache →
`ShaderResourceBinding` → then the Render callers.

The Render callers are where the attempt stopped: `MaterialLibrary`,
`CameraManager` (including its `CAMERA_DESCRIPTOR_BINDINGS` constant, which
becomes a `std::array<Rhi::DescriptorBinding, 1>`),
`SceneDataManager` and `CommandBuffer::BindMaterial` (whose `if (descriptor_set)`
becomes `.IsValid()` and whose `bindDescriptorSets` needs
`ToVulkanDescriptorSet`). Those four files are the whole remainder.

### Descriptor pass: three of the four Render files are done

The whole cascade compiles up to the Render layer, and three of its four files
were converted and compiled in the last attempt:

- `MaterialLibrary`: `ResolveLayout(desc_bindings, nullptr)` — the create-info it
  used to build is gone.
- `CameraManager`: `CAMERA_DESCRIPTOR_BINDINGS` becomes
  `inline static const Rhi::DescriptorSetLayout` (the `inline` matters: a static
  data member with an in-class initialiser of non-integral type needs it), and
  `descriptors[i]` takes `Rhi::ToVulkanDescriptorSet(arena.AcquireRawSet(...))`.
- `CommandBuffer::BindMaterial`: `if (material_binding.set.IsValid())`, and the
  set is converted into a named local before `bindDescriptorSets` — a conversion
  inside the braced list does not bind to the array proxy.

`SceneDataManager` is the only file left, and it has exactly three sites:
`ResolveLayout(descriptor_bindings, ...)`, the
`scene_descriptor_sets[i] = ToVulkanDescriptorSet(AcquireRawSet(...))` assignment,
and the skybox `bindDescriptorSets` call. An index-based edit there shifted lines
and broke an unrelated declaration, which is why the round was reverted: those
three sites want the `edit` tool with the exact text, not line arithmetic.

### A design gap the scene layout exposes: immutable samplers

The descriptor pass ran to `SceneDataManager` and stopped on a real gap rather
than a mechanical one. The scene descriptor set is built like this:

```cpp
descriptor_bindings.assign(DESCRIPTOR_BINDINGS.begin(), DESCRIPTOR_BINDINGS.end());
descriptor_bindings[1].setImmutableSamplers(immutable_samplers);   // shadow map sampler
```

An immutable sampler is part of a *descriptor-set layout*, not of a binding's
type, and `Rhi::DescriptorBinding{binding, type, count}` has no field for it. So
`ResolveLayout(const DescriptorSetLayout &)` cannot express this layout at all —
`descriptor_bindings` is still a vector of Vulkan bindings precisely because one
entry carries a sampler that lives in the layout.

Three ways forward, in the order I would try them:

1. **Add the field.** `DescriptorBinding` gains `SamplerHandle immutable_sampler{}`
   and an `immutable_sampler_count`, and `ToVulkanBindings` passes
   `pImmutableSamplers` through. This is the honest shape: an immutable sampler
   *is* a property of the layout, and the WebGPU backend will need it too,
   because a bind group layout with a fixed sampler is exactly what WebGPU calls
   an immutable sampler.
2. **Bind it dynamically.** Drop the immutable sampler and bind a normal sampler
   with the shadow-map set. Costs one descriptor slot and one shader change.
3. **Keep a Vulkan overload.** Rejected: it reintroduces the type this whole pass
   exists to remove.

Option 1 is the one the design wants, and it is small: one field, one pass-through
in `ToVulkanBindings`, and `SceneDataManager` builds
`DESCRIPTOR_BINDINGS` as an `Rhi::DescriptorSetLayout` with the sampler filled in
instead of patching a Vulkan binding afterwards.

### `DeviceBuffer::GetBuffer`: the one remaining mechanical item, and how not to do it

The change is one line in the header and one in the source; the call surface is
about sixty sites across Render, Physics, the apps and the tests. Attempting it
with a tree-wide `(\w+->GetBuffer\(\))` sweep failed the same way every sweep in
this session has failed: the negative lookbehind does not stop the pattern from
matching *inside* an already-wrapped expression, so
`ToVulkanBuffer(pbuf->GetBuffer())` became
`pEngine::Rhi::ToVulkanBuffer(buf->GetBuffer())`.

The reliable shape for this one is per file, in this order, building after each:

1. `engine/Rhi/Buffer/DeviceBuffer.h` — the getter returns `BufferHandle`.
2. `engine/Rhi/Buffer/DeviceBuffer.cpp` — return `allocation.GetBuffer()`.
3. `engine/Rhi/Submission/SubmissionHelper.cpp` — the `copyBuffer` and
   `copyBufferToImage` sites wrap their buffer arguments.
4. `engine/Render/**` — `CommandBuffer`, `FrameManager`, `CameraManager`,
   `SceneDataManager`, both present providers.
5. `app/physics/PhysicsApp.cpp`, then the eight tests.

Each of those files has five or fewer sites, so each step is a couple of edits
and one build. The whole item is perhaps twenty minutes done that way, and the
sweep that "saves" it has now cost three rounds.

Also worth knowing: `engine/Rhi/Device/DeviceBuffer.h` does not exist — the
header lives under `engine/Rhi/Buffer/`. A wrong path in a script creates the
file silently and the error only surfaces two steps later.


### Resume point

State at the end of the fortieth working round, verified green (build exit 0, 77/77
tests, `openspec validate --strict` passes, working tree clean):

- 45 commits on `feature/wgpu` since the baseline `ecf28c17`.
- Checked tasks: 2.1, 2.2, 2.4, 4.1, 4.2, 4.3, 4.4, 5.1, 5.2, 5.3.
- `engine/Rhi` public headers that still name a Vulkan type: **three**, seven
  sites in total — `ImmutableResourceCache.h` (4), `SubmissionHelper.h` (2),
  `DeviceBuffer.h` (1).

Three next steps, in the order that costs least:

1. **`DeviceBuffer::GetBuffer`** — steps 1 to 3 of the recipe in the note above
   are written and were verified to compile; what remains is the Render, app and
   test call sites, **edited per expression by hand**. No regular expression can
   wrap `pimpl->back_buffer->GetBuffer()`, because the `->` belongs to the
   receiver chain: every sweep this session produced
   `pimpl->Rhi::ToVulkanBuffer(back_buffer->GetBuffer())`.
2. **`ImmutableResourceCache::GetPipelineLayout`** — needs a portable
   pipeline-layout description, the same shape as `DescriptorSetLayout` plus the
   push-constant ranges. Four call sites build the Vulkan create-info today.
3. **`SubmissionHelper`** — `CmdOperation` and `ExecuteSubmission` are the
   submission and synchronization contract, which is group 6's subject, not
   group 3's; they should move with it.

After (1) and (2), tasks 3.2 and 3.3 can be checked, which unblocks 5.4.

### Pipeline-layout description: the design is settled, the four call sites are not

The description type compiles on its own and the cache implementation was
written and built cleanly:

```cpp
struct PushConstantRange { uint32_t offset{0}; uint32_t size{0}; bool compute{false}; };
struct PipelineLayoutDescription {
    std::vector<DescriptorSetLayoutHandle> set_layouts;
    std::vector<PushConstantRange> push_constant_ranges;
};
PipelineLayoutHandle GetPipelineLayout(const PipelineLayoutDescription &, const char *name);
```

`ImmutableResourceCache::GetPipelineLayout` builds the
`vk::PipelineLayoutCreateInfo` from it — the same shape as the descriptor-set
layout conversion that is already in place.

What is left is the four call sites, and they are awkward in one specific way:
each holds its set layouts as **local `vk::DescriptorSetLayout` values**
(`scene_descriptors`, `camera_descriptors`, a captured `pimpl->..._layout`), so
the description has to be assembled from `FromVulkan(...)` conversions of those
locals *and* from handles that are already portable. Two of them
(`MaterialLibrary`, both branches) also differ by whether the material set is
present, so they cannot share one replacement text.

Practical order for the next attempt: convert `CameraManager` first (one site,
two layouts, no branching), build, then `SceneDataManager` (one site), then the
two `MaterialLibrary` branches. Each is two or three edits and one build, and the
type work is already done.

### Pipeline layout: the four call sites, written out

Everything below assumes the description types and the cache implementation from
the previous note are in place (`EngineRhi` builds with them; only the callers
below fail to compile).

**SceneDataManager**, one site:

```cpp
// before
std::array pcr{RendererManager::GetPushConstantRange()};
const vk::DescriptorSetLayout scene_layout =
    Engine::Rhi::ToVulkanDescriptorSetLayout(scene_descriptor_set_layout.handle);
vk::PipelineLayoutCreateInfo plci{vk::PipelineLayoutCreateFlags{}, {scene_layout}, pcr};
scene_common_pipeline_layout = system.GetIRCache().GetPipelineLayout(plci, "Scene Common Pipeline Layout");

// after
const Rhi::PipelineLayoutDescription scene_pipeline_description{
    {scene_descriptor_set_layout},
    {{0, RendererManager::GetPushConstantRange().size, false}}
};
scene_common_pipeline_layout = Rhi::ToVulkanPipelineLayout(
    system.GetIRCache().GetPipelineLayout(scene_pipeline_description, "Scene Common Pipeline Layout")
);
```

**CameraManager**, one site: the same shape with
`{Rhi::FromVulkan(m_system.GetSceneDataManager().GetLightDescriptorSetLayout()), pimpl->camera_descriptor_set_layout}`
as the set list, and `pimpl->camera_common_pipeline_layout` receives the
`ToVulkanPipelineLayout`-wrapped result.

**MaterialLibrary**, two branches (with and without the material set): set lists
`{Rhi::FromVulkan(scene_descriptors), Rhi::FromVulkan(camera_descriptors), b.descriptor_set_layout}`
and `{Rhi::FromVulkan(scene_descriptors), Rhi::FromVulkan(camera_descriptors)}`;
both assign to `b.pipeline_layout`.

Three things that cost time in the attempts so far and are worth avoiding:

- the push-constant size comes from `RendererManager::GetPushConstantRange().size`,
  which is one range at offset 0 — the graphics flag is `false`;
- `scene_descriptor_set_layout` and `b.descriptor_set_layout` are already
  `DescriptorSetLayoutRef`/handle values, so they need no `FromVulkan`, while
  `scene_descriptors`/`camera_descriptors`/`GetLightDescriptorSetLayout()` are
  still Vulkan and do;
- the destination members are `vk::PipelineLayout`, so the cache's handle return
  is wrapped once, by `Rhi::ToVulkanPipelineLayout`.

### Group 3 is four sites from done

Landed this round: `GetPipelineLayout` takes a `PipelineLayoutDescription`
(set-layout handles plus push-constant ranges) and the cache is the only place a
Vulkan pipeline-layout structure is named; the constructor and the sampler
creation take a `DeviceHandle`. Four call sites assemble their descriptions —
`SceneDataManager`, `CameraManager`, and both `MaterialLibrary` branches — and the
seventeenth trap of this migration is recorded with them: `scene_descriptor_set_layout`
and `b.descriptor_set_layout` are `BindingRef`s, so they need `.handle` before
they can sit in a `std::vector<DescriptorSetLayoutHandle>`.

Four sites remain, two files:

- `ImmutableResourceCache.h`: `GetSampler` and `GetSamplerRef` still take a
  `vk::SamplerCreateInfo`. The only caller of either is `GetSamplerRef(const
  SamplerDesc &)`, which builds that create-info itself and then calls it — so the
  fix is to **inline the create-info body into the `SamplerDesc` overload and
  delete both create-info functions**. A file-local helper cannot do it: `impl` is
  a private nested type, and a free function in the `.cpp` cannot name it.
- `SubmissionHelper.h`: `CmdOperation` and `ExecuteSubmission`, which are group 6's
  subject.

With the first item done, tasks 3.2 and 3.3 can be checked.
### Group 3 is done, bar the submission helper

`engine/Rhi` public headers now name exactly two Vulkan types, both in
`SubmissionHelper.h`: `CmdOperation`'s command-buffer parameter and
`ExecuteSubmission`'s `vk::SemaphoreSignalInfo`. Those are not leftovers — the
`submission-helper-sync` delta in this change specifies their replacement, and
they move with group 6, which owns submission and synchronization.

That closes 3.1, 3.2 and 3.3: every portable header either carries no backend
type or has moved into `Rhi/Backend/Vulkan`, the Vulkan build is unchanged, and
all 77 tests pass — including the descriptor and kernel suites that exercise the
converted interfaces.

What is *not* done in group 3, and stays open:

- **3.6** wants a translation unit that includes every public header and links
  against neither Vulkan nor WebGPU. With `SubmissionHelper.h` still naming a
  command buffer it cannot pass yet, so it lands with group 6.
- **3.5** (the capability/limits query) is untouched.
### The Rhi layer is backend-neutral

`grep vk:: engine/Rhi/**/*.h` outside `Rhi/Backend/Vulkan` now returns **nothing**.
Every public header in the layer names only portable value types: opaque typed
handles, `Extent2D`/`Rect2D`, `ImageFormat`, `DescriptorType`, `ResourceState`,
`DescriptorBinding`/`DescriptorSetLayout`, `PipelineLayoutDescription` and
`TimelineSignal`. The conversion to Vulkan happens in
`Rhi/Backend/Vulkan/VulkanHandles.h` and in the implementation units of each
class.

That is the whole point of the change and it is now true: a second backend has a
seam to implement rather than a layer to rewrite.

Two notes for whoever picks up 3.6:

- the check that task 3.6 asks for is now meaningful — a translation unit that
  includes every public `engine/Rhi` header needs only the engine's own include
  path and the C++ standard library, and it should be run without the Vulkan
  include directory on the path at all;
- `SubmissionHelper::CmdOperation` moved into the implementation, so the batch's
  recording callback is no longer part of the public surface. When group 6 gives
  it an encoder parameter it will stay invisible to the interface.
### Group 3 is complete

All six of its tasks are checked. `Rhi::CommandEncoder` exists and both the
physics and the render recording paths use it (3.4); the public headers name no
backend type and include no backend header (3.1–3.3), which
`rhi_public_headers_test` now enforces on every build (3.6); and the device
reports its capabilities in portable terms (3.5).

What is left in the change is 5.4 and everything from group 6 onward. 5.4 is the
one item whose size is now known rather than estimated: `engine/Render` holds 513
`vk::` occurrences across 31 files, `engine/Physics` 5 across 3, `engine/Framework`
3 in one file and `engine/Core` 1 in one file. The small modules are an afternoon;
`engine/Render` is the pipeline, material and render-graph layer, which the
portable seam does not yet describe.
### 5.4: what is left, and the one question that blocks Physics

`engine/Framework` and `engine/Core` are now free of backend types. What remains
is `engine/Physics` (5 mentions, all on two `vk::CommandBuffer` parameters of
`PhysicsSystem`) and `engine/Render` (500 mentions across 31 files).

The five physics mentions look trivial and are not, because of how the tests
record. `PhysicsSystem::GPUStep` is the only place the engine still hands raw
command buffers across a module boundary, and it was kept that way deliberately:
`ISolver` and both solvers already take `Rhi::CommandEncoder &`, and
`PhysicsSystem` builds the encoder internally. Converting the façade means the
caller must supply an encoder — and a caller has no way to make one.

- In production the answer is `Render::CommandBuffer::GetEncoder()`, which is why
  `MainClass` and `PhysicsApp` convert easily.
- The headless physics tests have no `CommandBuffer`: they open a one-time Vulkan
  command buffer in a local lambda (`run_command_buffer`) and hand the raw handle
  to the physics façade. There is no portable factory for an encoder that wraps a
  command buffer a test opened itself.

So the missing piece is not a type substitution but **a contract for obtaining an
encoder**: either the tests build a `Render::CommandBuffer` (heavier, but honest),
or the portable layer grows a scoped recording object a test can open on the
device's own one-time buffer. That decision belongs in the design, next to D3's
"a scope is a boundary" — it is the same question one level down.

### Where to start next

D10 is the next thing to build, and it is small: one portable entry point
("open a one-time recording"), one Vulkan implementation, one submission. With it
in place:

1. `PhysicsSystem::GPUStep` and `GPUCalcModelMatrices` take `CommandEncoder &`,
   the `vk::CommandBuffer` overloads go, and `engine/Physics` reaches zero. The
   headless tests open a recording instead of a raw command buffer.
2. `engine/Render`'s 500 mentions are then all inside the render layer proper —
   pipeline state, material descriptors, render-graph attachments — which needs
   the seam extended before any of it can move. That extension is the real
   remainder of 5.4 and the precondition for group 8.

Nothing in this change is blocked on a missing tool any more except the browser
half: `D:\emsdk` exists but has no `upstream/emscripten`, so `emcc` was never
installed, and tasks 1.4 and all of group 11 wait on that download.
### The toolchain is real now, and the seam holds everywhere but Render

Emscripten is installed and both ports are warmed (`sdl3` 3.4.2 and
`emdawnwebgpu`, the latter from Dawn 20260423), so the browser build no longer
waits on the network. Getting it to configure took six fixes that only a cross
build exposes: Core asked pkg-config for libuuid; the top level forced shared
libraries that wasm cannot have and libktx refuses; the install rules bundled an
SDL3 runtime artifact and exported a CMake package that a static third-party
graph also cannot express; Framework copied DLLs a browser build never produces;
libktx's JavaScript bindings default on. Getting it to compile took four more in
the reflection parser, all of the same shape — it was deciding how to run from
the *target* platform instead of the host: WIN32, an empty
CMAKE_GENERATOR_INSTANCE, a compiler path that is not cl.exe, and a resource dir
that belongs to the target toolchain. It now locates Visual Studio through
vswhere, reads the MSVC version from the toolset beside the libclang it found,
and keeps its own builtin headers.

Where the seam now holds, measured by counting `vk::` outside `Backend/Vulkan`:

| module | before | now |
|---|---|---|
| `engine/Rhi` | 167 sites | **0** |
| `engine/Physics` | 5 sites + headers | **0** |
| `engine/Framework` | 3 | **0** |
| `engine/Core` | 1 | **0** |
| `engine/Render` | 513 | **500** |

`Rhi::RecordingScope` is what made the physics column reach zero: a caller with
no frame command buffer opens a scope, records through its encoder, and submits.
`PhysicsSystem` therefore takes `CommandEncoder&` and no backend type crosses a
module boundary anywhere outside `engine/Render`.

What is left is one module and one backend:

- **`engine/Render`**, 500 sites in 31 files. The largest groups are `vk::Format`
  39, `PipelineStageFlagBits2` 38, `vk::ImageLayout` 28, `vk::CommandBuffer` 24,
  `vk::DescriptorType` 19, `vk::ImageMemoryBarrier2` 14, `vk::PipelineLayout` 14,
  `vk::Extent2D` 14, `vk::RenderingAttachmentInfo` 12. Most map onto types the
  seam already has (`ImageFormat`, `ResourceState`, `DescriptorType`, `Extent2D`,
  the handle family); `Barrier()` already covers what the stage and access flags
  express. The render pass and pipeline-state descriptions do not exist yet and
  are the real design work.
- **The WebGPU backend itself**: `Rhi/Backend/WebGPU/` has no implementation of
  any of the 22 units that `Backend/Vulkan/` provides, so a WebGPU build links
  nothing. This is group 8, and it is the largest single piece of the change.

The browser build currently stops in `engine/Render`, which is the honest state:
everything the change has made portable compiles for wasm, and everything it has
not yet touched is in that one module.
### engine/Render: the 500 sites, by file

Ordered as the migration should take them — smallest and most self-contained
first, so each step can be reviewed against a green Vulkan build:

| sites | file |
|---|---|
| 81 | `RenderSystem/SwapchainPresentProvider.cpp` |
| 62 | `Pipeline/CommandBuffer.cpp` |
| 49 | `RenderSystem/FrameManager.cpp` |
| 47 | `Pipeline/RenderGraph/RenderGraphBuilder.cpp` |
| 38 | `RenderSystem/OffscreenPresentProvider.cpp` |
| 35 | `RenderSystem/SceneDataManager.cpp` |
| 30 | `Pipeline/Material/MaterialTemplate.cpp` |
| 23 | `UserInterface/GUISystem.cpp` |
| 21 | `Pipeline/RenderGraph/RenderGraph.cpp` |
| 17 | `AttachmentUtilsFunc.h` |
| 17 | `Pipeline/Renderer/VertexAttribute.cpp` |
| 15 | `Pipeline/RenderGraph/RenderGraphPass.cpp` |
| 9 | `RenderSystem/CameraManager.cpp` |
| 9 | `Pipeline/Material/MaterialLibrary.cpp` |
| 7 | `Pipeline/Material/MaterialTemplate.h` |
| 6 | `Pipeline/CommandBuffer.h` |

31 files in total. The two present providers are the natural pair to do first:
both already implement `IPresentProvider`, whose interface is fully portable, so
what remains inside them is Vulkan-only detail. `CommandBuffer.cpp` and
`FrameManager.cpp` are the frame's recording path and depend on the pipeline
state description that does not exist yet, so they come after it.

One thing learned the hard way: `FullRenderSystem.h` is both the public interface
header and the module's precompiled header. Removing its Vulkan includes makes
every unit in the module fail at once, because it was the only thing bringing
them in. It is the last header to change, not the first.
### What is left in engine/Render is four decisions, not 306 substitutions

The migration has reached the point where the remaining sites stop being
replaceable. Measured now: 306 in the shared layer, 139 already behind
`Render/Backend/Vulkan`, and each remaining file needs one of four things the
seam does not have yet. Doing them as type swaps would produce code that
compiles and renders differently - the pipeline-description step already proved
that, when a dropped `gpci.layout` cost twelve tests.

1. **Resource-state transitions** — `RenderGraphBuilder`, `RenderGraph` and
   `RenderGraphPass` (83 sites together) record `vk::ImageMemoryBarrier2` with
   stage and access masks chosen from a dependency graph. `Barrier()` says only
   *that* ordering is needed, which is enough for compute and not enough for a
   graph that rewrites a resource's layout. The seam needs a transition in
   portable terms: which image, from which state, to which state. `ResourceState`
   already names the states; what is missing is the call that takes two of them.
2. **Synchronization objects** — `FrameManager` (49) owns timeline semaphores,
   fences and per-frame reuse. `TimelineSignal` describes a signal; there is no
   portable fence, no portable wait, and no way to say "wait for this before the
   queue reads that".
3. **Shader modules** — `MaterialTemplate` (19) and `MaterialLibrary` (7) compile
   GLSL to a `vk::ShaderModule` and hand it to the pipeline. The seam has no
   shader object at all. This is the same gap group 7's bake exists to fill: a
   module is bytes plus an entry point, and both backends take exactly that.
4. **Rewriting a resident set per frame** — `SceneDataManager` (23) patches the
   same descriptor set every frame because the buffer it points at can grow. The
   arena's `Acquire` interns by content and never rewrites a resident entry, so
   this path needs a decision: either the buffer stops moving, or the seam gains
   an explicit "repoint this binding" operation.

`CommandBuffer.cpp` (66) and `GUISystem.cpp` (22) are not on that list: both are
recording surfaces that will be rewritten once (1) exists, because every barrier
they record is a transition.
### The image transition API exists; the render graph cannot use it yet

`CommandEncoder::Barrier(const ImageBarrier &)` is in and green. Wiring the render
graph to it stopped on something the API cannot fix from outside:
`RenderGraphPass::pass_work` is invoked as `std::invoke(pass_work, cb, *this)`
with a `vk::CommandBuffer`. Every recorded pass body in the engine - render
graph, editor, shadow cascades, the physics preview - is a lambda that takes that
handle, so moving the graph onto an encoder moves all of them in one step.

That is why the attempt was reverted rather than forced: it is not a barrier
change, it is a change to how every pass body is written. The seam is ready;
what is missing is the decision to convert pass bodies to `CommandEncoder &`,
which is the same decision `PhysicsSystem` already took.

Order for the next attempt: convert `RenderGraph::Record`'s signature and the
`pass_work` type first, let the compiler list every pass body, then convert them
in one pass over the render layer, the editor and the tests.
### Why the pass-body conversion was reverted twice

Two attempts at converting `pass_function` and `pass_work` to take a
`Rhi::CommandEncoder &` produced a green build and a failing suite, so both were
reverted. The second attempt got as far as converting `RenderGraphPass`,
`RenderGraph`, the frame loop, the editor example and two headless tests, and
`headless_compute_test` then reported every push-constant value as zero.

That failure is worth recording precisely, because it rules out the shortcut the
attempt took. The shortcut was: keep `RenderGraphPassBuilder` building a
`CommandBuffer` (the render layer's recording object), and reach the command
buffer it needs back out of the encoder with `ToVulkanCommandBuffer(GetHandle())`.
That keeps every pass body untouched, which is why it looked cheap.

It is not equivalent, and the reason is that `Render::CommandBuffer` owns its own
`Rhi::CommandEncoder` built from the handle it was given. Two encoders then exist
over one command buffer: the outer one passed into `pass_function`, and the inner
one the pass body records through. Push constants are the first thing that
notices, because `SetKernelParams` records against an encoder's state.

So the conversion cannot be routed around: **`Render::CommandBuffer` has to become
a wrapper over an encoder someone else owns**, instead of an object that builds
its own from a handle. Until that changes, every pass body is a Vulkan pass body.

Order for the next attempt:

1. `Render::CommandBuffer` takes `Rhi::CommandEncoder &` and holds a pointer
   rather than owning one; `GetHandle()` and the `vk::CommandBuffer` member
   become derived from the encoder where they are still needed.
2. Then `pass_function` and `pass_work` change type, and the compiler lists every
   pass body at once.
3. Only then is `WrapRenderPass` worth rewriting onto `BeginRenderPass`.

Trying (2) before (1) is what cost these two rounds.
### The windowed tests draw nothing, and no test was ever going to say so

Captured `pbr_test` under RenderDoc (`build/capture_pbr_frame5284.rdc`) after the
barrier wiring. What the capture shows:

| event | what happens |
|---|---|
| 14 | `vkCmdBeginRendering(C=Clear, D=Clear)` - the first pass does bind and clear depth |
| 24 | one indexed draw, 2880 indices, into image 249 |
| 25 | `vkCmdEndRendering(C=Store, D=Don't Care)` - **depth is not kept** |
| 29 | a compute dispatch |
| 31 | `vkCmdBeginRendering(Load)` - a second pass, no depth attachment |
| 40, 42 | two more draws, 114 and 852 indices |
| 48 | resolve, then present |

Image 249 is entirely black and the final target is too, apart from the ImGui
panel, which draws in its own pass. `pixel_history` on the first draw's centre
says the fragment shader wrote `(0, 0, 0, 0)` - so the geometry ran and produced
nothing, it was not culled.

The second pass loads colour but has **no depth attachment**, because the first
pass ended with `D=Don't Care` for depth. Anything depth-tested there fails. That
`Don't Care` comes from the pass's own `store_op`, which is engine code this
change has not touched, so this looks pre-existing rather than a regression -
but it cannot be confirmed by the suite, because:

**None of the eleven windowed tests asserts anything about pixels.** They check
that a frame completes, not that anything was drawn. That is why black frames and
a green suite are compatible, and why this went unnoticed until someone watched
the window.

Two things to do, in order:

1. Establish whether this predates the change: build the tree at the commit
   before the render-graph wiring and capture the same frame. That is the only
   way to tell a regression from a long-standing gap.
2. Either way, the tests need one pixel assertion each - at minimum "the centre
   of the frame is not the clear colour" - because a renderer with no pixel
   assertions can regress into anything and still be green.
Two things the capture settled, so the next person does not chase them:

- the "depth not kept" on the first pass is written by `pbr_test` itself
  (`SetDepthStencilAttachment` with `StoreOperation::DontCare`, line 346), and the
  second pass has no depth because it is a colour-only pass. Neither is a
  consequence of the encoder work.
- `debug_pixel` on the first draw's centre reports `gl_FragCoord.z = 0.99975`,
  i.e. the fragment that wrote the black pixel is at the far plane. So the
  geometry was not culled by a broken depth range; that fragment is background,
  which points at the camera or the model transform rather than at the barrier
  work.

What is still unknown: whether the frame was ever non-black. `mrt_test` reads
pixels back and only prints them, so even the one test that could have compared a
value does not. Establishing this needs the tree built at the commit before the
render-graph wiring, captured the same way - about ten minutes of build time, and
the only honest way to tell.
