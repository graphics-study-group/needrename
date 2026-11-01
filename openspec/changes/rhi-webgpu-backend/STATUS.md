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

Two assertions now exist, and one of them has been verified in both directions:

- `mrt_test` fails when its read-back colour target is uniformly zero.
- `skybox_test` fails when the frame is black. **With the zero write mask restored it
  exits 1 and says so; with the fix it reports 640000 of 640000 pixels not black.**
- `pbr_test` fails when fewer than one percent of the composed frame is non-black. It
  measures 805875 of 8294400 (9.7%), which is the sphere.

The other eight windowed tests still assert nothing about pixels. That is a known gap,
not an oversight.

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

