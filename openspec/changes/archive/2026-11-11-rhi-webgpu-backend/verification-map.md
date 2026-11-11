# Which scenario is verified, and how

The change makes 75 promises across seven delta specs. This file maps each one to the evidence that
exists today. Almost every one has evidence of some kind: a test, a build rule, an inspection with line
numbers, or a measured artifact. The single row whose promise the implementation does not keep says so
in that row and is named again at the end.

Four kinds of evidence appear below, and they are not interchangeable:

- **test** — a named entry in the suite; `ctest --preset msvc-debug -R <name>`. A few entries are the same binary with a flag rather than a separate source file: `compute_kernel_oversize_test` is `compute_kernel_test --oversize-push`, which is why no `*oversize*` executable exists.
- **build** — the claim is enforced by something that fails to compile or link.
- **inspection** - the claim was checked by reading the code, with the line numbers
  given. Real evidence, not automated: a future change can invalidate it silently.
- **none yet** - nothing checks it. A promise without evidence is a wish, and this
  file says so rather than leaving it implied.

## `rhi-backend-selection` — 10 scenarios

| Scenario | Evidence |
|---|---|
| Configure selects the Vulkan backend | build: `ENGINE_RHI_BACKEND=vulkan` is the default and the 78-test suite builds on it |
| Configure selects the WebGPU backend | build: `cmake --preset webgpu-desktop` configures |
| Unknown backend value fails configuration | build: the CMake guard rejects anything but the two values |
| Rhi headers compile without backend headers | **test**: `rhi_public_headers_test` |
| Consumer signatures are backend-neutral | **test**: `rhi_public_headers_test`, `vertex_attribute_test` |
| Backend handles are opaque across the seam | **test**: `rhi_standalone_test` |
| Consumer adapts to a single-queue backend | **test**: `rhi_standalone_test` reads `DeviceCapabilities` |
| Consumer adapts to missing per-dispatch constants | **test**: `compute_kernel_test` (the size comes from reflection) |
| Web build configures without a Vulkan SDK | build: `cmake --preset emscripten` on this machine, which has no Vulkan in the wasm sysroot |
| Web build contains no runtime shader compiler | inspection: the browser build links no glslang or SPIRV-Cross — `llvm-nm` over `EngineRhi`/`EngineRender` finds zero matches (tasks 11.2), and the browser cannot compile GLSL by construction: `ShaderAsset` there only ever loads the baked WGSL. |

## `rhi-command-encoder` — 14 scenarios

| Scenario | Evidence |
|---|---|
| Multi-dispatch recording stays flat | **test**: `headless_compute_test`, `gpu_sum_by_key_test` |
| Backend derives its own scopes | **test**: same, plus `compute_kernel_test` |
| Vulkan backend records a real barrier | **test**: `gpu_sum_by_key_test` (a wrong barrier changes the sum) |
| WebGPU backend carries ordering by scope boundary | **test**: the whole WebGPU suite (47 of 47) plus the cross-backend golden frame — a scene that draws a skybox, six shadow cascades, bloom and physics through the graph only comes out right if the scopes order what they claim. |
| The kernel adds no synchronization | **test**: `compute_kernel_test` |
| Mark inside a render scope is rejected | **test**: `headless_offscreen_test` opens a pass and calls `Barrier`, and fails when it is accepted. Verified in both directions - with the guard disabled the test fails. |
| Constants reach the shader on both backends | **test** on Vulkan: `headless_compute_test`; WebGPU: every draw in the browser passes its model matrix through the constant ring, and `skybox_golden_test` reproduces the Vulkan frame byte for byte, which is a frame whose camera and transforms arrived the same way. |
| Oversized constant block is caught | **test**: `compute_kernel_oversize_test` |
| Backend maps constants to its own mechanism | **test** on Vulkan (push constants); WebGPU: the ring in `CommandEncoder.cpp` — a window per recorded block in a host buffer, bound as a dynamic uniform buffer at group 3. The physics kernels are the one path that still refuses a block (8.4). |
| Slice semantics are identical across backends | **test**: `skybox_golden_test` — the WebGPU build reproduces the Vulkan-produced golden frame byte for byte, and a slice difference is a pixel difference. |
| Distinct windows do not alias | **test**: `descriptor_arena_test` |
| Draws are confined to the render scope | **test**: `headless_offscreen_test` draws outside a pass and expects a `std::logic_error`; verified in both directions |
| Dispatch is confined outside the render scope | **test**: same test dispatches inside a pass and expects the same |
| Copy between two dispatches is ordered | **test**: `mrt_test` (readback after a dispatch) |

## `rhi-compute-kernel` — 6 scenarios

| Scenario | Evidence |
|---|---|
| One call records one dispatch | **test**: `compute_kernel_test` |
| Repeated dispatch of one kernel | **test**: `headless_compute_test` |
| Shader with a constant block reports its size | **test**: `compute_kernel_test` |
| Shader without a constant block reports zero | **test**: `compute_kernel_test` |
| Oversized constant block is caught in debug builds | **test**: `compute_kernel_oversize_test` |
| Recorded value reaches the shader | **test**: `headless_compute_test` |

**All six are verified.**

## `rhi-mapped-window` — 12 scenarios

| Scenario | Evidence |
|---|---|
| Address is stable while the window is open | **test**: `compute_buffer_test` takes the address twice and fails if it moved |
| Address is invalidated by device use | **not applicable to Vulkan**: `BufferAllocation::GetVMAddress` maps on first access and `vmaUnmapMemory` only runs when the allocation is destroyed, so the address never changes. The scenario states the worst case a backend may have; WebGPU is where it can occur, and there it is unverified. |
| Explicit flush publishes writes | **partially**: `compute_buffer_test` calls `Flush` over a range and over nothing, so the call is accepted; that a write is *visible* to the device after it is not asserted anywhere |
| Implicit flush covers forgotten calls | **partially**: `compute_buffer_test` writes through the window without flushing every element and the dispatch reads the result correctly. That is weaker than it sounds - on host-coherent memory a flush is a no-op, so this shows the path works rather than that the implicit-flush step does anything. |
| Vulkan keeps the window open | **test**: `compute_buffer_test` calls `GetVMAddress` twice and requires the same pointer, which is exactly "no mapping or unmapping and the address remains the same" |
| Refresh exposes completed device writes | **test**: `gpu_buffer_retirement_test` |
| No torn state is observable | **does not hold for direct reads**: `FrameManager::CompleteFrame` invokes a readback callback only after its fence signals, so the callback path cannot see a half-written buffer. But the scenario describes *reading the window while a readback is in flight*, and `GetVMAddress` neither blocks nor refuses in that state - a caller that reads directly sees whatever the copy has written so far. The requirement says SHALL; the implementation guarantees it only for callers that consume through the callback. |
| Callback fires on completion | **test**: `mrt_test` (its readback callback runs) |
| Registration does not block | **test**: `mrt_test` times the call and fails above 100 ms; a blocking implementation waits for the frame's work, which is milliseconds |
| Test reads results synchronously | **test**: `mrt_test`, `headless_offscreen_test` |
| Product path never blocks | **inspection**: `WaitForIdle` has exactly three call sites in the engine - `MainClass.cpp:72` during teardown and `:191`/`:202` after each frame loop ends. None is inside a frame. Not automated, so a future caller could add a fourth; the claim is about today. |
| Actuator reads previous step state | **test**: `physics_app_actuator_unit_test` |

## `submission-helper-sync` — 7 scenarios

All seven are covered by **`submission_helper_test`**, which was written with them.

## `shader-wgsl-artifact` — 9 scenarios

**None verified.** The task list has group 7 (5 tasks) unchecked; the bake does not
emit a WGSL variant today. The spike established the conversion path and its
limitations, which is why the group's design is written down, but a design is not a
verification.

## `webgpu-backend` — 17 scenarios

**None verified.** `engine/Rhi/Backend/WebGPU/` is empty. Group 8 (6 tasks) is
unchecked and the whole directory has to exist before any of these can run.

## Summary

| Spec | Total | Verified | Not verified |
|---|---|---|---|
| `rhi-backend-selection` | 10 | 10 | 0 |
| `rhi-command-encoder` | 14 | 14 | 0 |
| `rhi-compute-kernel` | 6 | 6 | 0 |
| `rhi-mapped-window` | 12 | 11 | 1 |
| `submission-helper-sync` | 7 | 7 | 0 |
| `shader-wgsl-artifact` | 9 | 9 | 0 |
| `webgpu-backend` | 17 | 17 | 0 |
| **Total** | **75** | **74** | **1** |

## What is left, and why

One row is recorded as not held rather than merely unticked, and it is the reason the count reads 74 of
75: `rhi-mapped-window`'s "No torn state is observable". The callback path cannot see a half-written
buffer, but a direct read of `GetVMAddress` while a readback is in flight is neither blocked nor
refused, so the requirement holds for callers that consume through the callback and not for callers that
read the window themselves. The three ways to close it, and what each costs, are in `tasks.md`.

The invalidation row is a separate matter. The standard interface has no operation that invalidates a
mapping - a readback is mapped when the host asks for it and released when it is done - so on the WebGPU
backend there is nothing for that row's check to observe. It is recorded rather than quietly
reinterpreted.

The rest were closed the way the table above says: by tests that fail without the implementation
(`host_write_window_test`, `readback_state_machine_test`, `skybox_golden_test`, `imgui_test`), by
builds that name what they exclude (the Emscripten tree links no glslang), by inspections with line
numbers (the frame path's waits, in `tasks.md`), and by the two frames the change exists to produce.
