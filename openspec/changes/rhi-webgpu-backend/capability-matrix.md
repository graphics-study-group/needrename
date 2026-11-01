# The portable subset

This is what the seam promises, what each backend maps it to, and what a caller
must not do inside a scope. It is written to be checked against the code rather
than admired: every row names the header it lives in and the behaviour a backend
has to reproduce.

Status: the Vulkan column is what the engine does today. The WebGPU column is what
`Backend/WebGPU/` must do; it is **designed, not implemented** - `Backend/WebGPU/`
is empty, and this document says so rather than implying otherwise.

## Objects the seam owns

Every object below is created by a named constructor, moves, and cannot be copied.
Ownership is the object's: a submission that outlives the call which created its
resources does not need anyone to pair a create with a destroy.

| Object | Header | Created by | Holds |
|---|---|---|---|
| `DeviceContext` | `Device/DeviceContext.h` | the engine, once | the device, allocator, arenas, caches |
| `DeviceBuffer` | `Buffer/DeviceBuffer.h` | `CreateUnique` | a buffer with memory |
| `Texture` / `ImageTexture` | `Texture/Texture.h` | `CreateUnique` | an image, with its view and sampler |
| `CommandBuffer` | `Command/CommandBuffer.h` | `CommandBuffer::Create` | one recording |
| `CommandEncoder` | `Command/CommandEncoder.h` | over a command buffer | the recording calls |
| `Fence` | `Device/Fence.h` | `Fence::Create` | a host-waitable flag |
| `Semaphore` | `Device/Semaphore.h` | `Semaphore::Create` | an ordering point, binary or timeline |
| `ShaderModule` | `Pipeline/ShaderModule.h` | `ShaderModule::Create` | compiled code, with no stage |
| `Pipeline` | `Pipeline/Pipeline.h` | `Pipeline::Create` | a built pipeline |
| `RecordingScope` | `Command/RecordingScope.h` | `Create` | a one-time command buffer and its encoder |

## Values the seam names

These are the vocabulary that lets a caller describe work without naming an API.
They live in `RhiTypes.h` and `Pipeline/PipelineDescription.h`.

| Value | What it says | Not what it is not |
|---|---|---|
| `ImageFormat` | a pixel layout: 16 real formats plus `UNDEFINED` | not a Vulkan or WebGPU enum; both convert |
| `Extent2D`, `Extent3D`, `Offset2D/3D`, `Rect2D` | geometry | no backend has its own |
| `ImageAspect` | which faces of an image a range covers; a bit set | not one aspect at a time - a depth-stencil format has two |
| `MemoryAccessTypeImage` / `...Buffer` | how a resource is read or written | not a stage and not a layout |
| `PipelineScope` | what kind of work a barrier separates | not a stage name; the backend derives stages from it |
| `DescriptorType` | how a shader sees a resource | the eight kinds both APIs share |
| `ResourceState`, `FormatFeature` | portable state and capability | ignored by a backend that does not need them |
| `DeviceCapabilities` | four limits the engine asks about, plus `max_bind_groups` | the fifth was added because WebGPU's most binding limit had nowhere to live |
| `ShaderStage` | which stage a build runs a module as | belongs to the build, not to the module |
| `SubmissionDependency` | a semaphore, a timeline value and a stage | a backend cannot infer the stage, and a wrong one is a race, not an error |

## What recording guarantees

| Call | Guarantee | Vulkan | WebGPU (to implement) |
|---|---|---|---|
| `Barrier()` | compute work issued before it is visible to compute work after it | one `vkCmdPipelineBarrier2` with a memory barrier | a mark that splits the scope; no device call |
| `Barrier(ImageBarrier)` | the named image moves from one access description to another | layouts derived from the accesses; stages from the scopes | no transition exists; access is expressed by binding |
| `BeginRenderPass` / `EndRenderPass` | a pass over named attachments, with load and store stated once | `vkCmdBeginRendering` / `vkCmdEndRendering` | `beginRenderPass` / `endRenderPass` |
| `BindGraphicsPipeline`, `BindComputePipeline` | the following draws or dispatches record with this pipeline | `vkCmdBindPipeline` | `setPipeline` |
| `BindDescriptorSet` (compute) | the set is bound against the layout of the last pipeline bound | `vkCmdBindDescriptorSets` | bind group + dynamic offsets |
| `BindGraphicsDescriptorSet` | the set is bound against **the layout given**, because a scene, a camera and a material each own theirs | same | same |
| `PushConstants` | the block applies to what follows and has no identity | `vkCmdPushConstants` | a window in a ring, bound as a uniform buffer |
| `SetKernelParams` | the same, for compute | `vkCmdPushConstants` | a window in a ring |
| `Draw`, `DrawIndexed` | one draw with the counts stated | `vkCmdDraw` / `vkCmdDrawIndexed` | `draw` |
| `SetViewport` | the viewport and the scissor together, because they are one area stated twice | `vkCmdSetViewport` + `vkCmdSetScissor` | `setViewport` + `setScissorRect` |
| `CopyBuffer`, `CopyImageToBuffer`, `BlitImage` | a transfer with its two endpoints and its box | `vkCmdCopyBuffer` / `vkCmdCopyImageToBuffer` / `vkCmdBlitImage2` | `copyBufferToBuffer` / `copyTextureToBuffer` / a render-pass blit |

## What a caller must not do inside a render scope

These are the restrictions the WebGPU pass imposes, stated now so that code written
against the seam today does not have to be rewritten later.

1. **No transition inside a scope.** WebGPU has no layout transitions. A backend
   expresses the change by where the resource is bound, so a `Barrier` naming an
   image inside a pass is meaningless there. Transition before the pass.
2. **No pipeline change across a scope boundary that would rebind.** The Vulkan path
   allows binding a pipeline mid-pass; the WebGPU path rebinds at scope boundaries,
   which is why every dispatch gets its own scope.
3. **No reading a resource the scope writes.** The same rule both APIs state; the
   seam does not relax it.
4. **No host wait inside a frame.** A frame's work is ordered with submissions and
   fences; waiting inside one is what the mapped-window and readback designs exist
   to avoid.
5. **No assumption that a push constant survives a dispatch.** It is a parameter
   for what follows, not a resource.

## What the seam does not offer, and why

| Absent | Because |
|---|---|
| a portable `RenderPass` object with subpasses | the engine uses dynamic rendering; a subpass model would have to be invented for one backend and thrown away for the other |
| a portable query pool | nothing uses one yet; adding it before a consumer exists would fix a shape without evidence |
| a portable buffer transition | no consumer needs one; every buffer use today is covered by `Barrier()` |
| a portable image view object | a texture exposes its view; a standalone view has no consumer |
| backend-specific extension points in the public headers | a header that names Vulkan is what this change removed, and `rhi_public_headers_test` keeps it removed |

## How this document is checked

- `test/unit/rhi/rhi_public_headers_test.cpp` includes every public Rhi header and
  links without Vulkan, so the "no backend type in the public surface" claim fails
  the build if it stops being true.
- The Vulkan column is the implemented behaviour, exercised by the 78-test suite.
- The WebGPU column is the design; it is not implemented, and this document says so
  in its first paragraph rather than in a footnote.

