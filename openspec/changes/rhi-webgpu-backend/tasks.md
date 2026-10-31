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

- [ ] 4.1 Change the solver and detector interfaces from a raw command buffer to the command encoder, and verify the physics tests pass unchanged on the Vulkan backend
- [ ] 4.2 Convert `DispatchBarrier` call sites to `encoder.Barrier()` at unchanged positions and verify the Vulkan command stream for one physics step is identical to before
- [ ] 4.3 Route `ComputeKernel::Dispatch`'s constant argument through `SetKernelParams` and verify the reflected block-size assertion still fires on an oversized block
- [ ] 4.4 Verify `gpu_algorithm` recordings contain no new barrier and no new dispatch relative to the current Vulkan stream

## 5. Render and framework migration

- [ ] 5.1 Replace the raw command buffer in the render command recorder and the render graph's pass functions with the encoder, and verify golden images are unchanged
- [ ] 5.2 Replace `vk::Extent2D`/`vk::Format`/semaphore types in the presentation provider, attachment descriptions and frame manager interfaces, and verify windowed and headless presentation still work
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
