# Where this change stands

Written to be read before the code, by someone deciding what to do next. It says
what is real, what is not, and which of the two a given word in the other documents
refers to.

## The sentence that matters

**The seam exists and is enforced; the second backend does not exist.**

`engine/Rhi` is a portable interface: its public headers contain zero Vulkan names,
they compile and link without the Vulkan SDK (enforced by `rhi_public_headers_test`),
and every object a caller touches - buffers, textures, pipelines, command buffers,
fences, semaphores, submissions - is a portable type. The render layer records its
work through that interface and draws a frame.

`engine/Rhi/Backend/WebGPU/` is **empty**. Nothing in this change runs on WebGPU yet.

## Numbers, and what they mean

| Measure | Value | Read it as |
|---|---|---|
| tasks checked | 24 / 51 | groups 1-5 done; 6, 7, 8, 9, 10, 11 unfinished |
| requirement scenarios verified | 42 / 75 | 33 not, and the reasons differ - see `verification-map.md` |
| `vk::` in `engine/Rhi` public headers | **0** | enforced by a test, not a claim |
| `vk::` in the render layer's shared code | 61 (from 513) | 54 of those are the render graph's barriers, documented as abandoned |
| files in `Backend/WebGPU/` | **0** | the honest headline |
| tests | 78, all passing | including two pixel assertions that did not exist before |

## What was found along the way

Six things that were wrong, none of which any test noticed at the time:

**Three rendering bugs.**

1. **A zero colour write mask** made every 3D path draw nothing. `MaterialTemplate`
   padded a missing blend state with a value-initialised
   `vk::PipelineColorBlendAttachmentState`, whose `colorWriteMask` is zero. ImGui and
   the compute tests were unaffected, which is why only they had a picture.
2. **A barrier naming the wrong aspect and subresource range** in the render graph,
   introduced while wiring it to the encoder and caught by `shadow_map_test`.
3. **A frame encoder replaced while a command buffer still held it**, so push
   constants landed in the wrong object.

**One requirement the code does not meet.** `rhi-mapped-window` says a caller reading
the window while a readback is in flight sees the previous contents. That holds for
the callback path and not for direct reads. The three ways to fix it are in
`tasks.md`; choosing between them is a design decision, not a cleanup.

**One requirement that does not apply.** `Address is invalidated by device use` - on
Vulkan the mapping is created on first access and released only at destruction, so
the address never changes. The scenario describes a worst case; Vulkan chose better.

**One claim verified by reading rather than running.** `Product path never blocks`:
`WaitForIdle` has three call sites and none is inside a frame. Real evidence, not
automated - a fourth site would go unnoticed.

## The eleven windowed tests that passed while drawing nothing

The most useful thing this stretch produced is not a feature. Eleven windowed tests
ran for as long as the suite has existed, reported success, and **drew nothing at
all** - because none of them asserted anything about a pixel. A renderer can regress
into anything and stay green.

Six assertions now exist, and one of them has been verified in both directions:

- `mrt_test` fails when its read-back colour target is uniformly zero.
- `skybox_test` fails when the frame is black. **With the zero write mask restored it
  exits 1 and says so; with the fix it reports 640000 of 640000 pixels not black.**
- `pbr_test` fails when fewer than one percent of the composed frame is non-black. It
  measures 805875 of 8294400 (9.7%), which is the sphere.
- `new_material_test` does the same and measures 2073538 of 2073600, because the sunset
  fills the frame. This one found a real defect on its first run.
- `complex_mesh_test` does the same and measures 118270 of 921600.
- `imgui_test` fails when the frame is entirely black. Measures 374000 of 2073600 -
  the UI panel.

Four windowed tests assert nothing about pixels. Three of those (`input_test`,
`physics_registration_test`, `project_loading_test`) do not render a scene in the first
place, so there is nothing for them to assert. `shadow_map_test` keeps its
validation-error check instead - adding a pixel assertion there was tried and reverted
for the reason in its own section below. That is a known gap, not an oversight - and a
narrow one, since a wrong layout in that test is a validation error it already fails on.

## What is honestly unfinished

In the order a next session should take them:

1. **`Backend/WebGPU/` (group 8, 6 tasks, ~22 implementation units).** The whole
   point of the change. It cannot be done incrementally: `EngineRhi` does not link
   until every unit of the selected backend exists.
2. **The WGSL bake (group 7, 5 tasks).** The converter path is proven in the spike
   (Tint, with SPIR-V 1.3 and the push-constant rewrite) but nothing emits a WGSL
   variant today.
3. **The browser build (group 11, 3 tasks).** It configures and links a wasm library
   today - three object files and two support libraries, which is not a result worth
   reporting as one.
4. **The render graph's barriers (54 sites).** Eight attempts, every field proven
   identical, still red. Documented as abandoned with the diagnosis; whoever picks it
   up should start from the notes in `tasks.md`, not from scratch.
5. **The nine windowed tests with no pixel assertions.** Cheap, and it is the gap that
   let three rendering bugs live.

## What not to trust

- **`vk::` counts are a proxy, not a measure.** They dropped by 285 when duplicated
  `#include`s were removed, and by 11 when the last real conversion happened. Both
  were real work; neither is comparable to the other.
- **"verified" in `verification-map.md` has four meanings** - test, build, inspection,
  none yet - and they are spelled out there. A count that mixes them is a count of
  something, not of everything.
- **`Backend/WebGPU/` is empty.** Any document that reads as though the backend
  exists is wrong, including any that this one has not yet corrected.
## The fourth black frame, and what it turned out to be

Adding a pixel assertion to `new_material_test` made it fire: **0 of 2073600 pixels
non-black**, confirmed by a RenderDoc capture of a single six-index draw into a
1920x1080 target.

The readback path was not at fault - `pbr_test` reads 9.7% non-black through the same
`CopyImageToBuffer` call. The material was:

```cpp
test_material_instance->AssignTexture("base_texture", allocated_image_texture);
```

and the fragment shader declares

```glsl
layout(set = 2, binding = 1) uniform sampler2D base_tex;
```

`AssignTexture` does not complain about a name it cannot find; it binds nothing. The
sampler read zero, `base_color` was zero, and the scene was black. One word later the
capture shows the sunset.

The readback was reverted for a round while the cause was unknown, then reinstated once
it was: `new_material_test` now reads 2073538 of 2073600 pixels non-black and fails below
one percent.

**This is the fourth defect of the same shape.** The zero colour write mask, the barrier
with the wrong aspect, the frame encoder replaced under a live command buffer, and now a
texture bound to a name nothing reads - all four were silent, and all four survived a
suite that reported success.
## shadow_map_test was left without a pixel assertion, on purpose

Adding one was tried and reverted. The assertion itself passes - 2002636 of 2073600
pixels non-black - but the pass that makes the readback possible introduced ten
validation errors, and `shadow_map_test` already fails on validation errors.

The cause is the one this change spent eight attempts on and abandoned: that test
**rebuilds its render graph mid-run** (`rg = build_render_graph()` at line 969 to prove
shadow storage survives a rebuild). Adding a transfer pass gives the colour target a
final layout of `TransferSrc`, and the graph built on the next iteration expects it to
be a colour attachment. The layout does not come back because nothing transitions it
back - which is exactly the barrier work in the abandoned section.

So the test keeps its validation check and no pixel assertion. That is a smaller gap
than it sounds: a frame with a wrong layout is a validation error there, and the test
already fails on those.
## The WebGPU build links, and the library it links is empty

Measured rather than assumed:

| Build | `EngineRhi.dll` | object files |
|---|---|---|
| `msvc-debug` (Vulkan) | 4 870 656 bytes | 32 |
| `webgpu-desktop` | 324 608 bytes | 7 |

The seven are the reflection-generated units and the two hand-written headers'
helpers. **No backend implementation is in the WebGPU library at all**, because
`Backend/WebGPU/` is empty and `Backend/Vulkan/` is excluded by the backend switch.

So `cmake --preset webgpu-desktop && cmake --build ... --target EngineRhi` succeeding
means **"nothing was compiled"**, not "the WebGPU backend works". The same is true of
the wasm build, for the same reason.

## What a next session starts from

`tools/webgpu_spike/main.cpp` is 40 KB and already does the parts that are hardest to
get right without feedback: instance and device creation, adapter selection, a
triangle that rasterises and reads back, a compute dispatch whose results are checked,
and the limits query. It is a program, not a backend - it calls `wgpu.h` directly and
knows nothing about the seam - but every call it makes has to appear in
`Backend/WebGPU/` somewhere.

The order that avoids the empty-library trap:

1. Port the device creation and the allocation into `Backend/WebGPU/Device/`
   (`DeviceInterface`, `DeviceContext`, `AllocatorState`) and the buffer object.
2. Wire `ENGINE_RHI_BACKEND=webgpu` to compile **only** those, with the rest of the
   seam left as declarations. The library will not link - that is the point, because
   the missing symbols are the list of what remains.
3. Work down that list until `rhi_standalone_test` links; it is the smallest executable
   that exercises the seam end to end.

Step 2 is what makes this incremental. Without it, the only feedback is "it links
because nothing is there".
