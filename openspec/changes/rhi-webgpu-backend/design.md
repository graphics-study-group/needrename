## Context

See `proposal.md` for motivation. The state that shapes the approach:

- `engine/Rhi` classes are already pimpl-based (`DeviceInterface`, `DescriptorArena`, `ShaderResourceBinding`, `MemoryAllocation`, `SubmissionHelper`), so a header can stay fixed while its implementation unit is swapped per backend. What leaks today is types, not layout: `vk::Buffer`/`vk::ImageView` aliases in `BindingRef.h`, `vk::DescriptorSet` returns, `vk::CommandBuffer` parameters in `ISolver`/`CommandBuffer`, `vk::Extent2D`/`vk::Format` in presentation and attachments.
- The enabled Vulkan feature set is already the WebGPU shape: dynamic rendering + synchronization2 + timeline semaphores, no bindless, no descriptor indexing, uint32-only shader atomics, no geometry/tessellation, no MSAA, single-threaded CPU side. `MemoryTypes` are usage bits, not memory-property bits, so they map to WebGPU usage flags directly.
- Sync knowledge lives at the call sites: `DispatchBarrier` marks between physics dispatches, RenderGraph's usage-derived image barriers between its passes. Nothing relies on two dispatches overlapping except two independent one-thread clears.
- CPU access is `GetVMAddress()`/`Flush()`/`InvalidateMemory()` over permanently mapped VMA allocations, used in three shapes: host-written staging and per-frame uniform slices, device readback consumed one step late (`PhysicsApp` actuator loop), and synchronous readback in tests.
- Shader pipeline is offline already: `shader_baker` rewrites GLSL archives to embedded SPIR-V, physics `.comp` files are precompiled to a `.spv` tree, and `SPLayout::Reflect` runs on SPIR-V at runtime through SPIRV-Cross.
- OpenSpec is the planning home; `rhi-*`, `gpu-*`, `physics-*` capability specs describe current behavior and must keep describing it for the Vulkan backend.

## Goals / Non-Goals

**Goals:**

- One engine code base, one backend per build, Vulkan behavior preserved bit-for-bit as the migration baseline.
- The seam expressed as contracts callers already obey: flat recording with explicit sync marks, per-dispatch constants, resource windows, mapped-window CPU access.
- A WebGPU backend whose product form is the browser, and a desktop WebGPU build usable headlessly for cross-backend comparison.
- Shader artifacts that serve both runtimes from one asset set, with no runtime reflection on the WebGPU side.

**Non-Goals:**

- Runtime backend switching or two backends in one process.
- Third backends (DX12/Metal) beyond keeping the seam honest.
- Bindless/descriptor indexing, geometry/tessellation, MSAA, float atomics, indirect dispatch — none are used today and none are added.
- Platform features on the desktop WebGPU build (windowing, input, editor).
- Optimizing dispatch batching into shared compute scopes; correctness first, batching later.
- Migrating the shader source language (GLSL stays the source; WGSL is a baked artifact).

## Decisions

### D1. Backend selection is compile-time; headers are shared, implementation units are per-backend

`ENGINE_RHI_BACKEND=vulkan|webgpu` picks exactly one backend at configure time. Public headers keep their class shapes (pimpl) and are compiled against by both builds; each backend supplies the implementation units.

- *Alternative: runtime virtual interfaces.* Rejected: hot-path indirection, pressure toward the thinnest common denominator, and the A/B benefit it buys is obtained more cheaply by the desktop WebGPU build (D2).
- *Alternative: a standalone WebGPU renderer next to the engine.* Rejected: abandons RenderGraph/Material/Physics reuse and becomes a second product line.

### D2. The WebGPU backend is written against the standard WebGPU C interface

One adapter compiles for the browser (`--use-port=emdawnwebgpu`) and for a desktop WebGPU implementation (`webgpu.h` from wgpu-native first, Dawn if semantic drift bites). The desktop build exists only for validation and golden-image A/B; the browser is the behavioral reference.

- *Alternative: browser-only adapter.* Rejected: every A/B and every debug session would need a browser round-trip.
- *Alternative: a portable C++ abstraction above `webgpu.h`.* Rejected: a second seam to maintain when `webgpu.h` is already the common denominator.

### D3. Synchronization is explicit marks; WebGPU carries ordering in scope boundaries

The encoder is flat: `Dispatch`, draw, copy, `Barrier`, `TextureBarrier`. Marks mean "synchronize here" and are the caller's data-flow knowledge. Vulkan records a barrier at each mark and nothing between marks. WebGPU records one compute scope per dispatch and marks are no-ops — scope boundaries order everything, which is strictly more synchronization than the marks ask for.

- *Alternative: an implicit "dispatches are ordered" contract.* Rejected: it hides where sync is needed, forces barriers on Vulkan where none exist today, and it would rest on intra-pass ordering that the specification does not guarantee.
- *Alternative: marks as pass splits only where placed.* Cheaper in theory, but it makes correctness depend on complete mark coverage; the per-dispatch scope removes that dependency at the cost of scopes that match today's barrier count.
- Marks inside a render scope are rejected: WebGPU cannot express one without ending the render scope and changing load semantics.

### D4. Per-dispatch constants are kernel parameters, mapped per backend

`SetKernelParams(span)` carries the semantics; the reflected constant block is the size contract. Vulkan records real push constants while the block fits the device limit (falling back to a small uniform window otherwise). WebGPU writes a slice of a per-frame constant ring and binds a window for that dispatch.

- *Alternative: uniform windows everywhere.* Rejected: throws away Vulkan's zero-cost path for no portability gain — the interface already abstracts the mechanism.
- *Alternative: capability branching at call sites.* Rejected: pollutes physics and render code with backend knowledge.

### D5. CPU access keeps its call sites and becomes a mapped window

`GetVMAddress()` returns the current window address; `Flush()` closes the window and publishes writes; `InvalidateMemory()` refreshes the window from completed device work. Vulkan keeps the window permanently open, so its behavior and pointer stability are unchanged. WebGPU implements the window as a map state machine: host writes are accounted dirty and flushed by queue-ordered uploads before the consuming submission, readback opens the window when the copy completes. Address validity ends when the buffer next participates in device work — the same shape as today's reallocation invalidation.

- *Alternative: rewriting call sites onto an explicit staging API.* Rejected: churn across ~80 call sites for no semantic gain.
- *Alternative: a CPU shadow copy per readback.* Rejected: an extra copy on every readback; the mapped window gives the same pointer semantics without it.

### D6. Shader artifacts carry WGSL and an interface description as additive variants

The bake keeps the SPIR-V artifact and adds `.wgsl` plus an interface description (binding table, variable offsets/sizes/alignments, constant-block size and reserved binding, combined-sampler split map). One asset set serves both runtimes. The description is the single source of CPU-side placement truth; the bake validates that the produced WGSL agrees with it and fails on mismatch. The physics `.spv` tree gains the same side artifacts.

- *Alternative: a third mutually exclusive store type.* Rejected: two asset sets are needed for A/B and the Vulkan runtime would change for no reason.
- *Alternative: reflect at runtime from WGSL.* Rejected: reintroduces a reflection dependency in the web build and duplicates layout derivation.
- Converter is an external host-side tool invoked by `shader_baker` (naga first). If push-constant blocks do not convert faithfully, the bake rewrites the SPIR-V constant block into a uniform buffer before conversion.

  *Measured (see `spike-notes.md`):* the fallback is not optional. naga cannot read glslang's combined-image loads at all, cannot express a `std140` scalar-array stride, rejects the `OpSpecConstantOp` a specialization-constant comparison produces, and has no push-constant output. The bake therefore runs a rewrite pipeline before conversion — specialization constants demoted in the source, the constant block moved to a uniform buffer, opaque arrays flattened, write-only buffers widened. The interface description carries what the rewrites changed, so the runtime binds against the baked truth rather than a re-derivation.

### D7. Bind groups are built from the description and cached by content

Combined samplers expand to a texture entry and a sampler entry; constants bind at the reserved location; each window resolves to one bind group. The cache key is the bound resource identity plus each entry's window offset and size — the same content key the descriptor arena already uses — so repeated identical requests reuse one bind group.

- *Alternative: one bind group per dispatch.* Simple but wasteful for repeated identical bindings; the arena's keying already exists.

### D8. Frame completion is callback-driven; blocking survives only as a pump

Submission returns immediately. Completion callbacks advance the epoch watermark that resource retirement reads. A frame slot whose previous work is unfinished is skipped, not waited on. Synchronous readback exists as a device-event pump used by tests and load-time code; product paths consume the last completed snapshot.

- *Alternative: keeping fence waits with a yield shim.* Rejected: blocking is exactly what the browser host cannot do.
- *Alternative: worker threads with proxied blocking.* Rejected: the engine is single-threaded and the host isolation cost is unnecessary.

### D9. Work order: spikes, then interface excision with Vulkan green, then WebGPU

Two spikes precede any interface work: (a) bake 3 representative shaders through the converter and record every fidelity problem, especially the constant-block rewrite and layout agreement; (b) a minimal `webgpu.h` triangle plus one compute dispatch on the Emscripten toolchain, exercising canvas, limits and the frame loop. Then the interface is purified module by module with the Vulkan build and its golden images kept green, physics compute runs first on WebGPU, rendering and presentation follow.

- *Alternative: designing the full interface before touching a real WebGPU call.* Rejected: risks an abstraction fitted to imagined constraints.

### D10. A recorder is opened against a submission the caller owns, not handed to it

Recording and submission stay two separate concerns with one owner each: a caller
that already has a frame's `Render::CommandBuffer` asks it for its encoder, and a
caller that has no such object — a physics test, a one-shot upload — opens its own
submission scope and asks *that* for an encoder. The encoder never comes from a
command-buffer handle the caller made itself, because a handle is exactly what the
portable layer exists to hide.

The consequence is an addition to the seam rather than a substitution inside it:
a portable `begin-one-time-recording` entry point that owns a command buffer, hands
out an encoder, and offers `submit`/`wait`. Physics' façade then takes a
`CommandEncoder &` from either source and the `vk::CommandBuffer` overload on
`PhysicsSystem` disappears.

- *Alternative: let tests build a `Render::CommandBuffer`.* Rejected as the
  general answer: it works, but it puts a frame-lifecycle object in the way of
  code that only wants to record once, and it makes the physics tests depend on
  the render system's shape.
- *Alternative: keep the raw-handle overload for tests.* Rejected: it is the only
  place a backend type is still handed across a module boundary, and it would
  outlive the migration.

## Risks / Trade-offs

- [Converter fidelity: SPIR-V constant blocks, combined samplers, layout edge cases] → Spike (a) measures it before design is frozen; the bake falls back to a pre-conversion SPIR-V rewrite of the constant block, and layout validation fails the bake rather than shipping a mismatch.
- [Texture upload row alignment: three tight-packing sites violate WebGPU's 256-byte `bytesPerRow`] → the upload path is fixed once in the staging layer and verified in spike (b).
- [Limits: `maxBindGroups` defaults to 4 while rendering uses scene/camera/material plus a constants window] → request higher limits at device creation with a documented fallback; verified in spike (b).
- [No scaling blit on WebGPU: `BlitColorImage` and mipmap generation] → same-size copies map to `copyTextureToTexture`; mipmap generation becomes per-level render passes drawing a full-screen quad.
- [wasm32: 32-bit `size_t`, 4 GiB address space, physics buffer sizes] → sizes are computed from counts; a limit check at buffer creation reports rather than wraps.
- [One compute scope per dispatch costs pass overhead] → it matches the barrier count the code records today; batching independent dispatches is a later, local optimization that needs no contract change.
- [Desktop WebGPU semantics drift from the browser] → the browser stays the reference; the desktop build is a regression harness, and drift is caught by the same golden images.
- [Blast radius of removing backend types from 66 non-`Rhi` files] → mechanical, module by module, with the Vulkan build and its tests green at every step.
- [Render scope cannot contain sync marks] → the encoder rejects them; no current code path needs one.

## Migration Plan

1. Spikes land as throwaway branches; their findings feed the bake tool and the adapter skeleton.
2. Interface excision proceeds per module (`Rhi` headers first, then Physics, then Render/Framework), each step keeping the Vulkan build green and its golden images unchanged; the existing tests become the portable contract's test bed.
3. Shader bake gains the WGSL variant behind an opt-in flag; Vulkan keeps reading SPIR-V throughout.
4. WebGPU compute path lands first (physics), compared against Vulkan through the desktop build's headless readback.
5. Rendering, presentation and the Emscripten build land last.
6. Rollback at any step is "configure with `ENGINE_RHI_BACKEND=vulkan`", which is the unchanged baseline until the change is complete.

## Open Questions

- Converter choice (naga vs Tint) can be revisited if the spike shows fidelity gaps; both are host-side bake tools and neither affects the specs.
- Which desktop WebGPU implementation stays (wgpu-native vs Dawn) is a validation-harness detail.
- Whether browser-side tests run under a headless browser harness, and when that harness is worth building, is deferred until the Emscripten build exists.

