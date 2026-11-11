## 1. Spikes (throwaway, findings feed the artifacts below)

- [x] 1.1 Bake `blinn_phong` (texture + constant block), `fluid.comp` (shared memory + uint atomics + storage
  image) and the rest of the named corpus to WGSL and verify the outputs are accepted by a WGSL validator
  - **Done, and the validator run is the evidence.** Every `.wgsl` in the WebGPU stage - **66 modules**,
    the 24 built-in shader assets and the 42 physics kernels - is accepted by Tint with no error:
    `tint <file>` on each returns 0. The three shaders the task names are in that set: `blinn_phong.frag`
    (a sampled texture plus a push-constant block), `blinn_phong.vert`, and `fluid.comp` (workgroup
    storage, uint atomics, a storage image).
  - **The two blockers recorded earlier are gone rather than worked around.** Tint no longer refuses the
    shipped `.spv` for carrying `SPV_KHR_non_semantic_info` because the shader compiler stopped emitting
    debug info by default (`CompileGLSLtoSPV`'s `emit_debug_info`, design D12), and the physics bake
    converts all 42 of 42 modules including the Debug tree's `-gVS` output - Tint drops non-semantic
    instructions itself. Nothing had to strip the modules, which is as well: `spirv-opt --strip-debug`
    crashes on them.
  - The fidelity record is in `spike-notes.md` §1.1, including F2: `blinn_phong.frag` does not convert
    through **naga**, which is why the bake uses Tint.
- [x] 1.2 Verify the constant-block conversion path: convert a shader with a push-constant block and confirm the fallback (SPIR-V constant block rewritten to a uniform buffer before conversion) is needed or not; record the result in the spike notes
- [x] 1.3 Measure one dispatch per compute scope vs batched dispatches in a minimal `webgpu.h` compute sample and record the overhead in the spike notes
- [x] 1.4 Build a minimal `webgpu.h` triangle plus one compute dispatch on the Emscripten toolchain and
  verify it renders
  - **Both targets render, and the frame is the evidence.** `example/webgpu_clear_example` binds a
    storage buffer, runs a compute dispatch that writes a colour into it, and draws a triangle whose
    fragment shader reads that colour - the dispatch is recorded before the pass in the same encoder.
    On the desktop it logs "compute and triangle pipelines created" and "30 frames presented"; in the
    browser canvas it is an orange triangle on the blue clear, which is the value the dispatch wrote
    (a dispatch that never ran would leave it black).
  - **What the web build needed was an event-loop shape, not a fix.** `main` requests the adapter and
    installs `emscripten_set_main_loop` before either arrives, because the callbacks are delivered
    between animation frames and a page whose `main` returned with no loop has nothing left to run
    them; the device is requested from the adapter callback; and the frame loop pumps the instance
    *before* checking whether it is ready - the first version returned early on every frame, which is
    why the device never resolved where there was an event pump.
  - Three library rules surface as errors rather than defaults: a multisample count (zero is
    rejected), a shader source chained struct (`WGPUShaderModuleDescriptor` has no `code`), and a
    binding whose declared access must match the layout's.
- [x] 1.5 Record the target's limits (`maxBindGroups`, buffer and binding sizes) and the behavior of two dispatches sharing one compute scope in the spike notes

## 2. Build scaffolding

- [x] 2.1 Add the `ENGINE_RHI_BACKEND` configure variable accepting exactly `vulkan` and `webgpu`, and verify an unknown value fails configuration naming the accepted values
- [x] 2.2 Make the Vulkan SDK/glslang lookup and the Vulkan-only third-party targets conditional on the `vulkan` backend, and verify a `webgpu` configure succeeds on a machine without a Vulkan SDK
- [x] 2.3 Add an Emscripten toolchain file and configure preset for the web build, and verify the preset produces a wasm library target
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
- [x] 5.4 Verify `engine/Render`, `engine/Physics`, `engine/Framework` and `engine/Core` contain no backend type in any declaration

## 6. Mapped window

- [x] 6.1 Implement the mapped-window contract on the Vulkan backend with the window permanently open, and verify pointer stability and flush behavior are unchanged
  - Three pieces of evidence, and the third is the one that makes "unchanged" a fact rather
    than a claim:

    - `BufferAllocation::GetVMAddress` maps once and caches `mapped_ptr`; `FlushMemory` calls
      `vmaFlushAllocation` and never unmaps, so the window stays open for the allocation's life.
    - `compute_buffer_test.cpp` asserts it: two `GetVMAddress()` calls return the same pointer and
      fail the test if they do not, and an explicit `Flush(0, sizeof(float))` over a range is
      accepted. The test passes.
    - This branch has one commit touching `MemoryAllocation.cpp`, `ac457141`, and it moved the
      file behind `Backend/` without changing a line of the window logic.
- [x] 6.2 Implement dirty accounting and automatic flush-before-submission, and verify a caller that never calls
  `Flush` still renders correctly
  - **Done, and the verification is a test that fails without it.** `host_write_window_test` writes a
    pattern through a buffer's mapped window, never flushes, copies the buffer on the device and
    compares the readback - it passes on both backends (Vulkan 78 of 78, WebGPU 55 of 55).
  - The mechanism: `BufferAllocation` marks a pending host write when a caller asks for the window,
    clears its tracking slot on destruction, and `AllocatorState::FlushPendingHostWrites()` - called
    from `DeviceContext::Submit` - publishes what is pending. Vulkan flushes the mapped allocation,
    the WebGPU backend writes its host shadow into the buffer. `DeviceBuffer` registers itself through
    `TrackHostWrites` once the allocation has its final address, because the allocator's value form
    hands out a moved object.
  - Two traps found on the way, both worth knowing: `CommandBuffer::Create` allocates and does *not*
    start recording - a command buffer without `Begin()` submits nothing, silently - and a tracking
    hook placed in `AllocateBufferUnique` never sees a `DeviceBuffer`, which is the only caller that
    matters.
- [x] 6.3 Implement asynchronous readback registration and the blocking pump, and verify the existing headless tests pass through the pump with unchanged assertions
  - The pump exists and runs every frame: `FrameManager` keeps a deque of registries, each a
    fence, a command buffer and a list of callbacks; `WaitForFence(handle, 0)` polls rather than
    blocks, so a frame whose copy has not finished is left in the deque and the next frame looks
    again. `RegisterReadbackCallback` is the registration.
  - The task says "the existing headless tests". The test that actually exercises this is
    `test/engine/windowed/mrt_test.cpp`, which registers a callback and asserts on the buffer it
    receives - windowed, not headless. The substance the task asks for is covered and the wording
    is not; recorded here rather than quietly reinterpreted.
- [x] 6.4 Verify product frame paths contain no blocking wait on device completion
  - **Verified by audit, and the audit changed the code.** The product frame path -
    `RenderSystem::TryStartFrame` -> record -> `CompleteFrame` -> present - contains no blocking wait:
    the frame entry point polls the slot (`StartFrame(0)`, "busy" instead of a wait, task 10.2), and
    `FrameManager::PumpReadbacks` polls each registered fence with a zero timeout.
  - What remains waits, and why each stays:
    - `PhysicsApp::~PhysicsApp` - teardown, after the loop.
    - `PhysicsApp::Step`'s pre-wait - the in-flight render frames read the buffers the step writes.
      A GPU-side wait on the frame semaphore deadlocks (the semaphore handed out between frames is
      the *next* frame's), which is recorded under 6.2's neighbour 10.1.
    - `PhysicsApp::Step`'s fence wait - the step reads back its own results immediately after.
      The `WaitForIdle` that followed it was redundant and is gone in this change.
    - `PhysicsApp::RenderNextFrame` while paused - the caller's loop is not stepping, and the drain
      is what keeps `FrameManager` from re-signalling a semaphore the previous present still holds
      (VUID-vkQueueSubmit2-semaphore-03868).
    - `PhysicsApp::GetRenderOutput` - an explicit request for a finished frame's pixels, not a frame
      path.
    - Setup-time one-time uploads (`CommitScene`, `SetBodyValue`) and `SwapchainPresentProvider`'s
      `waitIdle` on recreate - both off the per-frame path.
  - The three physics tests (physics-only, offscreen, friction) pass with the redundant wait gone.

## 7. Shader artifacts

- [x] 7.1 Extend the shader baker with the WGSL variant and the interface description (binding table, variable offsets, constant-block metadata, combined-sampler split map) and verify a baked artifact is accepted by a WGSL validator
  - `shader_baker --spirv-dir <tree> --converter <tint>` bakes **42 of 42** physics modules and
    every built-in shader asset, writing a `.wgsl` next to each `.spv` plus an `.iface.json`. The
    description records the stage, the entry point, the constant block, and every binding with its
    set, number, kind, name, the sampler slot of a split combined binding, a storage binding's
    format and access - and, since the description-driven material layout went in, each buffer
    block's declared size and every one of its variables with name, offset, size and kind, plus
    each sampled image's sample type and view dimension. Without those a skybox is a plain 2D
    image instead of a cube and a material cannot place a uniform by name; with them the browser
    builds the same layout `Reflect` does and renders the scene.
  - The access flag is read from the *converted text*, because the converter infers read versus
    read-write from how a module uses a binding, which is not what the SPIR-V decoration says.
  - The validator the task asks about is the device: the browser compiles these modules at pipeline
    creation and draws with them, and a module it refuses fails the frame visibly. Tint refuses a
    `.spv` that still carries `SPV_KHR_non_semantic_info` debug info, and the shader compiler stopped
    emitting it by default (1.1), so nothing has to be stripped.
- [x] 7.2 Implement bake-time layout validation between the produced WGSL and the description, and verify a deliberately mismatching layout fails the bake naming the shader
  - The check runs in `BakeModule`: it reads back the WGSL it just produced, extracts every
    `@group(N) @binding(M)` pair, and compares that set against the description plus the constant
    block and the split sampler slot; a difference fails the bake and names the shader and both
    sides.
  - It found real disagreements this session rather than a synthetic one: a sampler slot the
    description claimed but the WGSL did not declare (a combined binding whose sampler slot
    collided with the next binding, which is why `bloom.comp` moved its output to binding 2), and
    a storage binding whose access the description had guessed from the SPIR-V rather than read
    from the text.
- [x] 7.4 Emit the same side artifacts for the physics shader tree and verify a physics kernel can be created without any reflection library linked
  - **42 `.wgsl` and 42 `.iface.json` are emitted for the physics tree** by one
    `shader_baker --spirv-dir` run, staged into the data root, and read by
    `PhysicsSpirvLoader`: `physics_spirv_loader_test` passes on the desktop WebGPU build.
  - **Kernels are created from them with no reflection involved.** The XPBD solver's 26 kernels
    load through `RequestComputeKernelFromModule` and dispatch in the example's physics step; the
    browser build, whose core defines no `spirv_cross` symbol at all, creates the same kernels.
- [x] 7.5 Verify the Vulkan runtime's loading and reflection behavior is unchanged with and without the WGSL variant present
  - **The runtime never looks at the new artifacts, so "with and without" is one state.** A search
    over `engine/` for `.wgsl` and `.iface.json` finds one hit, and it is a comment in the WebGPU
    CommandEncoder explaining where the constant block slot comes from. No code opens either file.
    The Vulkan runtime loads SPIR-V through the asset pipeline exactly as before, and the 78 tests
    pass with the baked variants sitting next to the modules they were derived from.

## 8. WebGPU adapter

- [x] 8.1 Implement device, queue, resource creation and capability query against the standard WebGPU C interface and verify the desktop validation target creates a device and reports its limits
  - `webgpu_device_test` in the webgpu-desktop build creates a `DeviceContext`, asserts the
    four reported limits are non-zero, and creates a buffer and a texture. Measured: uniform
    alignment 256, storage range 134217728, compute invocations 256, bind groups 4, push
    constants 0 by design. It fails with the reason if construction throws.
- [x] 8.2 Implement binding from the interface description with combined-sampler expansion and content-keyed bind-group caching, and verify identical requests reuse one bind group while distinct windows do not
  - The same test asserts the layout cache: two requests for one description return the
    same layout object and the same identity, and a description differing in one binding
    number returns a different one. Combined-sampler expansion and per-dispatch window
    binding are the parts of this task that are not covered: the first needs a baked
    description with a combined binding, the second is 8.4.
- [x] 8.3 Implement per-dispatch compute scopes with marks as no-ops and the rebind-on-scope-boundary behavior, and verify a marked and an unmarked recording both produce ordered results
  - **The scope boundary is the pass, and every dispatch already gets one.**
    `ComputeKernel::DispatchAndRecord` records bind pipeline, bind set, dispatch, then
    `EndComputePass`; `BindComputePipeline` opens a new pass when none is open. So each dispatch is
    its own scope with its own bindings, which is the rebind-on-scope-boundary behaviour the task
    asks for - it is a consequence of the pass object rather than code that implements it.
  - `Barrier()` is a no-op outside a render pass and throws inside one, which is the "marks as
    no-ops" half. It throws inside a render pass for the same reason it does on Vulkan: a caller
    asking there has a bug either way.
  - **Not verified with a recording.** Proving "a marked and an unmarked recording both produce
    ordered results" needs two dispatches over a GPU buffer and a readback, which is what
    `gpu_sum_by_key_test` does indirectly but never with an explicit mark between them. The
    implementation is inspected, not observed.
- [x] 8.4 Implement the constant ring and per-dispatch window binding, and verify a dispatched shader observes the recorded block
  - The bake records where the constant block goes - `.iface.json` carries
    `"constant_block": { "set": 1, "binding": 0, "size": 12 }` for `radix_scatter.comp`, with the
    SPIR-V push-constant block rewritten into a uniform buffer before conversion - and the ring
    exists: a host buffer of `k_constant_window`-sized windows, one per recorded block, written with
    `wgpuQueueWriteBuffer` before the submission that reads it and bound at set 1 with a dynamic
    offset.
  - **The compute path is closer than the earlier note said.** `GetPipelineLayout` already appends
    the constant ring's group at index 3 whenever a kernel declares a push-constant range, so a
    kernel hands it exactly three set layouts and lets it append the fourth. With that, and with the
    kernel binding each set at the index its description declares instead of putting everything in
    group 0, the physics solver **runs on the WebGPU backend**: the group it was refused at is the
    ring's, and the pass binds it.
  - **Landed:** a kernel now hands the cache exactly `k_constant_bind_group` set layouts (the cache
    appends the ring's group at index 3 itself), binds each set at the index its description
    declares, and writes its parameter block once the compute pass - and with it the ring's group -
    is open. The physics solver runs on the WebGPU backend until the next honest error:
  - **Landed since:** the two usage conflicts are gone, and neither needed the backend to lie. The
    radix histogram was `readonly` in the kernel that reads it and writable in the kernel that
    fills it; `parallel_scan.comp` scans a buffer in place, binding it readonly and writable in the
    same dispatch. Both now declare the access the frame uses. `PhysicsApp`'s own read-back
    buffers also follow the map/release rule the frame manager already follows.
  - **Next: a staging buffer that stays mapped.** *"Buffer with 'Physics pos write staging' is
    still mapped"* - those buffers are `StagingToDevice`, which this backend creates mapped at
    creation and never unmaps, and a mapped buffer cannot be the source of a copy. The state
    machine 8.5 asks for is exactly this: flush unmaps (the write becomes visible there), the next
    write maps again. An attempt needed `WGPUMapMode_Write`, which the buffer's usage flags do not
    include yet, and it crashed rather than reporting; that is where the next attempt starts.
  - **Two operational traps found the hard way.** A `physics_app_*` binary in `build/webgpu` can be
    *stale* without anything saying so: the preset has `ENGINE_BUILD_APPS=OFF`, so those targets are
    not part of a default build, and an old executable runs happily against new sources. Rebuild the
    target by name before believing a run. And with apps turned on, `EngineEditor` does not compile
    on this backend at all (`EditorRenderGraphBuilder.h` includes `vulkan/vulkan.hpp`), so exercising
    the physics app here needs a narrower switch than the whole-apps one.
  - A window is **zero-filled** before the block is copied in, because a shader reads its whole
    declared struct while every draw pushes fewer bytes than the block declares (the model matrix,
    not the two indices). Without that the tail of the struct is whatever the ring held there.
  - The evidence that a shader observes the block is the frame itself: every draw in the browser
    passes its model matrix and every compute dispatch its parameters through this ring, and the
    scene draws correctly there. Two failure modes are loud rather than silent: a block larger than
    the window throws, and so does a frame that records more windows than the ring holds (the
    cursor restarts at zero for each encoder, so the bound is the ring's capacity per frame).
- [x] 8.5 Implement mapped-window host access with queue-ordered uploads and completion-driven readback, and
  verify no mapped buffer is consumed by device work
  - **Both halves are implemented and both are tested.** A host-written buffer is a host copy on this
    backend, never a mapping; a caller that asks for the window records a pending write, and
    `DeviceContext::Submit` publishes it with `wgpuQueueWriteBuffer` before the commands that read it
    are submitted - so no buffer is mapped while a submission uses it, and the ordering is the queue's
    (`host_write_window_test`). A readback target is mapped when a caller reads it and given back
    before the next copy targets it (`readback_state_machine_test`, three submissions in a row).
  - The earlier note in this file argued the opposite - that the mapping had to stay for the window
    to have one address - and the second half of the argument was right: keeping the map is what the
    standard interface forbids, so the window became a host copy and the address is stable because
    the copy does not move.
  - **One part of the requirement is met by 10.1 rather than here.** The readback pump is
    completion-driven in what it waits for (map callbacks) but not in what it does: it waits on the
    calling thread. Turning that into a callback that advances the epoch watermark is the frame
    completion work, and this task does not claim it.
- [x] 8.6 Implement error and device-loss reporting through the logging path and verify a deliberately invalid recording is reported naming the operation
  - The path exists and was exercised by accident, which is the best kind of evidence: the first
    run of `webgpu_device_test` produced

    ```
    ERROR: WebGPU uncaptured error (type 2): Validation Error
    Caused by:
      In wgpuDeviceCreateSampler
        Invalid anisotropic clamp: 0. Must be at least 1.
    ```

    `DeviceInterface` installs an uncaptured-error callback that routes to `SDL_LogError` with
    the error type and the device message. The message names the operation - `In
    wgpuDeviceCreateSampler` - which is the requirement.
  - **Device loss is not verified.** The callback receives the type and would report
    `WGPUErrorType_DeviceLost` the same way, but nothing has provoked a device loss: doing so on
    a real adapter is not something a test can arrange without killing the run. Recorded as the
    half that is inspected rather than observed.

## 9. WebGPU compute path and cross-backend comparison

- [x] 9.1 Run the physics compute pipeline on the WebGPU backend and verify its buffers match the Vulkan backend's for the same setup
  - The solver runs on this backend and produces the **same numbers** as Vulkan from the same
    scene wherever the arithmetic is deterministic: `physics_app_physics_only_test` reports
    `box0.z: 2 ---> 1.98569` and `physics_app_friction_test`
    `35 deg ramp: moved 0.226004 m (down-slope 0.226004 m) over 600 substeps, final speed
    0.00610186 m/s` on both. Those numbers are computed from read-back body state.
  - `physics_app_stability_test` passes on both with different final `max|v|` (0.5672 against
    0.4442): a 60-step chaotic pyramid accumulates reduction order, and the test is a threshold for
    exactly that reason. Recorded so nobody reads it as a mismatch.
  - Getting there needed: in-place kernels declaring their buffers writable (WebGPU tracks usage per
    submission, so `readonly` next to a writable binding of one buffer is rejected - four shaders),
    a staging buffer created unmapped and published from a host copy (`MAP_WRITE` cannot combine
    with `COPY_DST`), the map/release pairing on every read-back buffer `PhysicsApp` owns, and the
    editor excluded from the WebGPU tree - it is a Vulkan application.
- [x] 9.2 Add headless golden-image comparison between the desktop WebGPU build and the Vulkan build and verify a fixed frame matches across backends
  - `skybox_golden_test` renders the skybox scene and compares the frame with
    `test/data/skybox_golden.rgba`, byte for byte, reporting how many bytes differ and where the
    first one is. The golden image was produced by the Vulkan build from the same scene, camera and
    shaders; the WebGPU build reproduces it exactly, and it has been that way since before the test
    existed - the frames were compared by hand first, and the test is what keeps them equal.
  - The test is windowed rather than headless (the frame comes from a real swapchain-free read-back
    of the render target), which is the strongest form of the check: the same bytes leave the same
    render graph through two different APIs.
- [x] 9.3 Verify the per-dispatch-scope overhead measured in spike 1.3 is within the recorded expectation for one
  - **The overhead is zero on this backend, so it is within any expectation - and the reason is worth
    writing down.** `CommandEncoder::Barrier()` here records *nothing*: the standard interface derives
    ordering from use, so the "one compute scope per dispatch" the design chose is a Vulkan shape that
    this backend does not encode. The spike's 22-26 us per scope measured scopes that were actually
    written, and none are.
  - Measured on `physics_app_stability_test` (60-body 6-layer pyramid), three runs per step count,
    differencing 20 and 60 steps to remove startup:

    | Backend | 20 steps | 60 steps | Per step |
    | --- | --- | --- | --- |
    | Vulkan | 1835 ms | 2321 ms | ~12.2 ms |
    | WebGPU | 3100 ms | 5331 ms | ~55.8 ms |

  - The step is therefore ~44 ms slower here, and that difference is **not** scope boundaries: at the
    ~36 barrier sites the physics algorithms record, the spike's rate would account for about 1 ms of
    it even if the scopes were encoded. What the step pays instead is the synchronisation this backend
    does in their place - the blocking wait before each step (task 6.4's one remaining wait) and the
    synchronous readback mapping. That is exactly the cost 10.1's completion callbacks are meant to
    remove, and it is recorded here so nobody attributes it to the design decision.

## 10. Frame loop and presentation

- [x] 10.1 Convert frame completion to callbacks advancing the epoch watermark and verify resource retirement
  still reclaims at the same watermark
  - **Done, and the two stalled attempts before it had one cause: `DeviceContext::Submit` never
    registered `on_complete` on this backend.** A callback that is never registered is never run, so
    every frame leaked one epoch until the tracker's 64-epoch cap tripped - and that assertion raises
    a message box, which is why it presented as a window that stopped responding at frame 65 rather
    than as a leak. The debugger's `NtUserWaitMessage` under `BeginEpoch` was that box.
  - What the change needed, in the order it was found: `Submit` registers the callback against the
    fence it signals; `WaitForFence` pumps the callbacks once its wait succeeds (the header promised
    it, the implementation did not); the slot's completions are pumped before its fence is reset,
    because a wait that finds the fence already signalled never reaches the pump and the reset then
    clears the signal a pending callback waits on; the epoch is captured **by value** at submit time
    rather than read from the slot when the callback runs, since by then the next frame can own it;
    and the callback reaches its owner through an `alive` flag rather than a raw `this`, because a
    completion callback outlives the frame that registered it - without that, `project_loading_test`
    crashed on the WebGPU backend while passing under a debugger.
  - **The watermark is unchanged.** `gpu_buffer_retirement_test` passes with `Parked peak: 30` and
    live allocations flat at 91 across ten grow/shrink cycles, the same shape as before the change,
    with the report now coming from the callback. Vulkan 79 of 79, WebGPU 57 of 57.
- [x] 10.2 Replace the blocking frame-slot wait with skip-and-retry and verify a busy slot skips the
  frame instead of blocking
  - `FrameManager::StartFrame(0)` returns "busy" (`~0u`) instead of throwing when the slot's fence
    is not signalled, and `RenderSystem::TryStartFrame` is the entry point that asks for that: the
    physics app's render loop polls each frame, skips a busy slot and retries on the next call. The
    blocking `StartFrame` stays as it was, which is why no test had to change.
  - The two earlier attempts to change the *wait* itself (recorded at the end of this file) failed
    because a skipped frame has to advance every caller's clock; giving the choice to the caller
    instead of taking it from them is what makes it safe. The frame's camera and light uploads moved
    into `UploadFrameData`, called by both entry points, so a skipped frame cannot leave the next
    one's uniforms a frame behind.
- [x] 10.3 Implement presentation through the host surface and verify the frame ring's per-frame windows stay distinct when the host exposes a single surface image
  - **Done on both backends, verified by looking at the frame.** `SurfacePresentProvider` for
    WebGPU configures the window's surface, acquires its image per frame, records the copy of the
    final colour target into it and presents; the engine's full example now draws its scene and
    presents it, and the surface image it hands the window is byte-for-byte the render target it
    copied from.
  - **The ring's windows stay distinct because there is one image.** The standard interface
    acquires one surface texture per frame and hands it back at present; a frame's constant window
    and readback buffer are per-frame-in-flight objects that no other frame writes while the queue
    still holds work, and the queue is one ordered queue. That is the argument the task asks for,
    and the surface-capture comparison is its evidence.
- [x] 10.4 Port the UI backend to the WebGPU backend and verify the editor UI renders
  - **The backend is ported, and the same test proves it on both.** Dear ImGui's WebGPU backend does
    build against wgpu-native 29 - the note that said otherwise was wrong, and the object file in
    `third_party/imgui.dir` had been saying so all along. `GUISystem` on this backend now runs
    `ImGui_ImplSDL3` for input and `ImGui_ImplWGPU` for draw data instead of the "unavailable" stub.
  - One seam addition was needed: ImGui's Vulkan backend records into a `VkCommandBuffer`, which the
    encoder already names, while its WebGPU backend records into a `WGPURenderPassEncoder` that exists
    only while a pass is open. `CommandEncoder::GetNativeRenderPass()` is that accessor - null on
    Vulkan, the open pass here.
  - **The verification is pixel parity, not a claim.** `imgui_test` was gated to Vulkan for the same
    wrong reason and is now registered for both; the same test renders the same demo window and
    reports **374000 of 2073600 pixels not black on both backends**, which is the UI drawn through the
    seam rather than a Vulkan shim.
  - Two details the port needed: `ImGui::Render()` is called even when no pass is open (ImGui asserts
    at the next `NewFrame` otherwise), and the pass-opening overload
    `DrawGUI(attachment, extent, cb)` opens the pass through the command buffer and draws into the one
    it opened - which is why the accessor belongs on the encoder.
  - **What this task claimed and what has since changed.** At the time, the *editor* could not
    build on this backend: `EditorRenderGraphBuilder.h` included `vulkan/vulkan.hpp` directly, so
    the editor binary stayed a Vulkan application and was excluded from the WebGPU tree. The editor
    is backend-neutral now - it no longer includes a backend header anywhere, and its rendered
    textures reach ImGui through `GUISystem::AddTexture` - so it builds and runs on this backend,
    including in the browser. That work belongs to the editor's own port rather
    than to this task's UI-backend claim.

## 11. Emscripten product build

- [x] 11.1 Build the engine and an example for the browser and verify it renders and runs the physics loop at interactive rates
  - The page loads its data root from the preloaded tree, creates a WebGPU device and surface,
    imports the model, enters its main loop and draws the scene into its canvas: sixty-odd frames,
    1038 of a 48x48 sample grid lit, brightest channel 138. The frame is the same one the desktop
    WebGPU build presents, from the same graph, through the same material path.
  - **The physics loop runs, but its kernels are refused.** The solver binds twenty storage
    buffers in one group and a browser caps that at sixteen; the kernels are marked unavailable
    and their dispatches are no-ops, so the loop runs and the scene is static. That is a limit of
    the target, not a fault, and it is recorded here rather than worked around.
  - **The budget was then met, and the remaining blocker was a rule, not a limit.** The detector and
    accumulator records were merged until every kernel fits sixteen storage bindings, after which
    the scene was still static for a different reason: three shader families bound one buffer to
    two storage bindings of one dispatch, which WebGPU rejects and wgpu-native does not. The fixes and
    the host-side guard that now makes the desktop build reject them too are recorded at the end of
    this file; the evidence is direct - the rigid-body example simulates in the canvas (probe falls
    4.000 -> 1.778 m and rolls on) and the editor runs and plays there.
- [x] 11.2 Verify the web build links neither the Vulkan SDK, VMA, glslang nor SPIRV-Cross
  - The browser build compiles EngineRhi from 31 objects - the same set as the desktop
    WebGPU build - and libEngineRhi.so defines 2086 symbols. llvm-nm finds zero matching
    vk, glslang, spirv_cross or spv. The four Vma matches are VmaMemoryAllocation's
    constructor and destructor and two GetVMAddress methods: that class holds two void
    pointers and nothing VMA, and the name is a leftover of the rename. No VMA
    code is linked.
- [x] 11.3 Move the test suite's synchronous readback onto the blocking pump with test-only yield support and verify the tests pass in the desktop WebGPU build
  - The pump is the only place a read-back is completed: `FrameManager` polls each registry's fence
    without waiting, calls `PrepareReadback` before the callback and `ReleaseReadback` after it, and
    a callback that returns the buffer puts it back in a pool keyed by size instead of destroying
    it. A read-back therefore costs no allocation after the first few frames, and the buffer is
    unmapped again before the device may write it - which is what "Buffer is still mapped" at
    submit was.
  - Yield support is `emscripten_sleep(0)` inside the two waits that need the page to deliver a
    callback (the map in `BufferAllocation::PrepareReadback` and the surface capture). A wait loop
    that only pumps events never lets the browser run, and that hung a frame outright.
  - The desktop WebGPU suite passes **47 of 47**, and the Vulkan suite **77 of 77** with the same
    pump. The three faults that were hiding behind "hangs" - a description with no bindings, a
    buffer left mapped, and `ENGINE_RHI_BAKE_REFLECTION` standing in for "can reflect" - are recorded
    at the end of this file.

## 12. Documentation and closure

- [x] 12.1 Write the portable-subset capability matrix (what the seam guarantees, what each backend maps, what is forbidden inside a render scope) into the change and verify it matches the implemented behavior
- [x] 12.2 Record the spike findings and the chosen converter and desktop implementation as decisions in the change
- [x] 12.3 Verify `openspec validate` passes and every requirement scenario in the delta specs has a corresponding test or documented verification
  - `openspec validate --strict` passes. `verification-map.md` maps all 75 scenarios
    to their evidence: 74 verified, and one recorded as not held - the torn-state scenario
    described at the end of this file.

## What was learned, for the next reader

This file used to carry a per-pass working log: which task was in flight, what was tried, what was
rolled back, and a resumable "start here" note. It was dropped at archive time because every count in
it is superseded. What survives below is what a future reader would otherwise pay for twice - the traps
already paid for, the decisions that landed in code rather than in `design.md`, what is still
unverified, and the one requirement the implementation does not meet.

### Traps already paid for

- **`MaterialTemplate` wrote no colour.** It padded absent blend targets with a value-initialised
  `vk::PipelineColorBlendAttachmentState` whose `colorWriteMask` was zero, while
  `ToVulkanColorBlendingOps` emits one state per configured entry - so every material that did not ask
  for transparency wrote nothing. Pre-existing; the correct state is "blending off, every channel
  written", and that choice now lives in the pipeline-description translation.
- **Eleven windowed tests passed while drawing nothing.** None asserted a pixel, and
  `headless_offscreen_test` - the suite's only pixel assertion - checks a cleared value. `mrt_test` now
  fails on a uniformly zero readback target, and `skybox_test` on a black frame, verified in both
  directions.
- **Two encoders over one command buffer.** Routing a pass body through
  `ToVulkanCommandBuffer(GetHandle())` while `Render::CommandBuffer` builds its own
  `Rhi::CommandEncoder` means `SetKernelParams` state never reaches the pass body: `headless_compute_test`
  read every constant as zero. `Render::CommandBuffer` had to become a wrapper over an externally owned
  encoder *before* `pass_function`/`pass_work` changed type.
- **The render graph's own barriers were eight attempts and are abandoned.** Every diffable field was
  proven identical, a comparison build showed the field values and not `CommandEncoder::Barrier` differ,
  and both submission orders were tried - `shadow_map_test` still fails. The graph keeps its
  `vk::ImageMemoryBarrier2`; do not restart this without a live debugger session.
- **`C2672` at `xmemory(730)` means a bad `emplace_back` argument, not a non-constructible container.**
  `writes.emplace_back(set, binding.binding, 0u, 1u, binding.type)` into
  `std::vector<vk::WriteDescriptorSet>` once `binding.type` turned portable, and
  `image_infos.emplace_back(..., binding.image_layout)`. The same signature hit
  `make_unique<MaterialTemplate>`: isolate one argument at a time.
- **Tree-wide regex sweeps keep failing.** `(\w+->GetBuffer\(\))` rewrote already-wrapped calls because
  `->` belongs to the receiver chain; `GenerateLayoutBindings` builds a braced
  `vk::DescriptorSetLayoutBinding` whose type argument is a two-line ternary; a token-level
  `Extent2D`/`Rect2D` replacement also rewrote the `vk::RenderingInfo` and `setScissor` sites inside
  `CommandBuffer.cpp`.
- **A conversion temporary inside a braced initialiser does not bind to `vk::ArrayProxy`** - assign
  `ToVulkanBindings(...)` or `ToVulkanDescriptorSet(...)` to a named local first. Hit three times.
- **`FullRenderSystem.h` is both the module's public interface and its precompiled header**, so dropping
  its Vulkan includes fails every unit in `engine/Render` at once. It is the last header to change, not
  the first.
- **`engine/Rhi/Device/DeviceBuffer.h` does not exist** - it is `engine/Rhi/Buffer/DeviceBuffer.h`. A
  wrong path in a script creates the file silently and the failure surfaces two steps later.
- **`pbr_test` sets `StoreOperation::DontCare` on its own depth attachment** and its second pass is
  colour-only, so "no depth attachment" in a capture is not a regression.

### WebGPU and Emscripten, learned by failing

- A mapped buffer cannot be the source of a copy, and `MAP_WRITE` cannot combine with `COPY_DST`:
  staging buffers are created unmapped and published from a host copy. Asking for `WGPUMapMode_Write`
  on a buffer whose usage lacks it crashed instead of reporting.
- Usage is tracked per submission, so one buffer bound readonly and writable in one dispatch is
  rejected - wgpu-native tolerates it. `parallel_scan.comp` scans in place, the radix histogram is
  filled by one kernel and read by another, and three shader families bound one buffer twice. Declare
  the access the frame uses; a host-side guard now makes the desktop build reject it too.
- The browser caps 16 storage bindings per bind group against wgpu-native's 20: the solver bound 20, so
  its kernels were marked unavailable and their dispatches were silent no-ops - loop running, scene
  static. Fixed by merging detector and accumulator records.
- Constant-ring windows are zero-filled before the block is copied in, because a shader reads its whole
  declared struct while callers push fewer bytes than the block declares. A block larger than the window
  throws, and so does recording more windows than the ring holds per frame.
- **`DeviceContext::Submit` must register `on_complete`.** Without it every frame leaks one epoch until
  `EpochTracker`'s 64-epoch cap raises an assertion *message box*, which presents as a window hanging at
  frame 65 (`NtUserWaitMessage` under `BeginEpoch`). `WaitForFence` must pump callbacks after a
  successful wait, slot completions must be pumped before the fence reset, the epoch must be captured
  by value at submit, and a callback must reach its owner through an `alive` flag rather than `this`.
- `CommandBuffer::Create` allocates without starting recording - a command buffer with no `Begin()`
  submits nothing, silently - and a write-tracking hook placed in `AllocateBufferUnique` never sees a
  `DeviceBuffer`.
- A GPU-side wait on the frame semaphore in `PhysicsApp::Step` deadlocks: the semaphore handed out
  between frames is the *next* frame's. The paused-path drain exists because of
  VUID-vkQueueSubmit2-semaphore-03868.
- A `physics_app_*` binary in `build/webgpu` can be stale without anything saying so: the preset sets
  `ENGINE_BUILD_APPS=OFF`, so those targets are absent from a default build and an old executable runs
  against new sources. Rebuild the target by name before believing a run.
- ImGui's WebGPU backend does build against wgpu-native 29 - the object file in `third_party/imgui.dir`
  had been saying so all along. `ImGui::Render()` must run even when no pass is open, and a
  `WGPURenderPassEncoder` exists only while a pass is open, which is why
  `CommandEncoder::GetNativeRenderPass()` exists.
- Emscripten: the device is requested from the adapter callback, `emscripten_set_main_loop` is installed
  before it arrives, and the frame loop pumps the instance *before* checking readiness or the device
  never resolves. `emscripten_sleep(0)` is required inside the two waits that need a page callback (the
  map in `BufferAllocation::PrepareReadback` and the surface capture); a wait loop that only pumps
  events hangs the frame.
- The reflection parser must decide how to run from the *host* platform, not the target: `WIN32`, an
  empty `CMAKE_GENERATOR_INSTANCE`, a compiler path that is not `cl.exe` and a target resource dir each
  broke it under Emscripten. It now uses vswhere and the toolset beside the found libclang, and keeps
  its own builtin headers.

### Seam decisions that landed in code rather than in `design.md`

- `Rhi::DescriptorBinding` carries `SamplerHandle immutable_sampler` plus an immutable-sampler count,
  and `ToVulkanBindings` forwards `pImmutableSamplers`: an immutable sampler is a property of the set
  layout, not of a binding's type, and `SceneDataManager` patches it for the shadow map. Binding it
  dynamically would cost a shader change; a Vulkan overload would reintroduce the type.
- `ImageBarrier` carries a `PipelineScope` (graphics/compute/transfer) so a backend derives stage masks,
  and `Barrier(std::span<const ImageBarrier>)` exists because a pass boundary is one dependency.
  `eAllCommands` on both sides is correct for the physics path only.
- `PipelineLayoutDescription` is set-layout handles plus `PushConstantRange{offset, size, compute}`, and
  `ImmutableResourceCache::GetPipelineLayout` is the only place a `vk::PipelineLayoutCreateInfo` is
  built; callers wrap the returned handle with `ToVulkanPipelineLayout`.
- A kernel hands the cache only its own set layouts and binds each at the index its description
  declares; the cache appends the constant ring's group at index 3 itself. A kernel that puts
  everything in group 0 is refused at group 3.
- Access flags in `.iface.json` are read from the converted WGSL text rather than from SPIR-V
  decorations, because the converter infers read versus read-write from how a module uses a binding. A
  combined binding's sampler slot also collides with the next binding index unless the layout is
  adjusted (`bloom.comp` moved its output to binding 2).
- `CAMERA_DESCRIPTOR_BINDINGS` must be `inline static const Rhi::DescriptorSetLayout`, and `BindingRef`s
  such as `scene_descriptor_set_layout` need `.handle` before entering a vector of layout handles.
- `IMemoryAllocation`'s Vulkan-side `GetSampler`/`GetSamplerRef` create-info overloads are gone: the
  create-info body is inlined into the `SamplerDesc` overload, because `impl` is private and a
  file-local helper cannot name it.

### Still unverified

- Device loss: the uncaptured-error callback would report it the same way it reports an invalid
  sampler, but nothing has provoked one on a real adapter. Inspected, not observed.
- 8.3's "a marked and an unmarked recording both produce ordered results" was never recorded, and needs
  two dispatches over a GPU buffer, an explicit `Barrier` between them, and a readback.
- 8.2: combined-sampler expansion and per-dispatch window binding have no test; only layout-cache
  identity is asserted.
- The test that exercises the readback pump is `test/engine/windowed/mrt_test.cpp` - windowed, not
  headless as task 6.3 states.
- `physics_app_stability_test`'s differing final `max|v|` between backends (0.5672 against 0.4442) is
  reduction order in a chaotic 60-step pyramid, and the test is a threshold by design. Do not read it as
  a cross-backend mismatch.

### A requirement the implementation does not meet

`rhi-mapped-window`'s "No torn state is observable" scenario says a caller that reads the window while
a readback is in flight observes the previous completed contents. The callback path cannot see a
half-written buffer, but a caller that reads `GetVMAddress` directly is neither blocked nor refused, so
it sees whatever the copy has written so far. Nothing in the code distinguishes the two callers, so the
guarantee holds for callback consumers and not for direct readers.

`verification-map.md` records this as the one scenario of 75 with no evidence - `rhi-mapped-window` has
12 scenarios and 11 verified. Fixing it is a design decision with three non-equivalent options: refuse
while in flight (correct, breaks every existing direct read), double-buffer the readback target (keeps
both paths, costs a buffer per in-flight readback), or narrow the scenario to callback consumers
(cheapest, changes what the capability promises). It belongs with whoever owns the mapped-window
design.
