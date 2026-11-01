# Which scenario is verified, and how

The change makes 75 promises across seven delta specs. This file maps each one to
the evidence that exists today, and says plainly which have none.

Three kinds of evidence appear below, and they are not interchangeable:

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
| Web build contains no runtime shader compiler | **none yet** — see the note at the end |

## `rhi-command-encoder` — 14 scenarios

| Scenario | Evidence |
|---|---|
| Multi-dispatch recording stays flat | **test**: `headless_compute_test`, `gpu_sum_by_key_test` |
| Backend derives its own scopes | **test**: same, plus `compute_kernel_test` |
| Vulkan backend records a real barrier | **test**: `gpu_sum_by_key_test` (a wrong barrier changes the sum) |
| WebGPU backend carries ordering by scope boundary | **none yet** |
| The kernel adds no synchronization | **test**: `compute_kernel_test` |
| Mark inside a render scope is rejected | **test**: `headless_offscreen_test` opens a pass and calls `Barrier`, and fails when it is accepted. Verified in both directions - with the guard disabled the test fails. |
| Constants reach the shader on both backends | **test** on Vulkan: `headless_compute_test`; WebGPU: **none yet** |
| Oversized constant block is caught | **test**: `compute_kernel_oversize_test` |
| Backend maps constants to its own mechanism | **test** on Vulkan (push constants); WebGPU: **none yet** |
| Slice semantics are identical across backends | **none yet** (needs both) |
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
| `rhi-backend-selection` | 10 | 9 | 1 |
| `rhi-command-encoder` | 14 | 12 | 2 |
| `rhi-compute-kernel` | 6 | 6 | 0 |
| `rhi-mapped-window` | 12 | 8 | 4 |
| `submission-helper-sync` | 7 | 7 | 0 |
| `shader-wgsl-artifact` | 9 | 0 | 9 |
| `webgpu-backend` | 17 | 0 | 17 |
| **Total** | **75** | **42** | **33** |

## The four kinds of "not verified"

They are worth separating, because they call for different work. The four add up to
33, which is what the table above says.

**Blocked on an unbuilt backend (26).** All nine in `shader-wgsl-artifact` and all
seventeen in `webgpu-backend`. Nothing can check these until groups 7 and 8 exist;
writing a test now would test nothing.

**Needs both backends (2).** The two cross-backend rows in `rhi-command-encoder` -
slice semantics and the WebGPU side of constants reaching the shader.

**Not applicable to this backend (1).** `rhi-mapped-window`'s invalidation scenario,
for the reason given in its row.

**Verifiable today but not written (4).** These are the ones that should bother a
reader. `No torn state is observable` is counted here too: it is verifiable in the
sense that a test would show it failing, which is the point - see "a requirement the
implementation does not meet" in `tasks.md`.

- *Web build contains no runtime shader compiler* - checkable by scanning the wasm
  library's imports once it is non-trivial.
- *Explicit flush publishes writes* and *Implicit flush covers forgotten calls* -
  both are marked partially: the calls happen and the results are right, but on
  host-coherent memory a flush is a no-op, so neither shows the flush step doing
  anything. Proving it needs memory that is not coherent, or a deliberate fault
  injected into the flush path.
- *Web build contains no runtime shader compiler* - see above.

That second group is what a reviewer should look at first: it is the difference
between "the design is not finished" and "the design says something the code does
not do".
## One attempt at the mapped-window tests, and why it is not in the tree

A `mapped_window_test.cpp` was written and then deleted rather than committed. It
set up a `DeviceInterface` and an `AllocatorState` the way
`buffer_reallocation_test` does, created a `CopyTo` buffer with
`DeviceBuffer::CreateUnique`, and asserted three things: that two `GetVMAddress()`
calls return the same pointer, that a write through the window reads back, and that
both `Flush` forms are accepted.

It hung. The binary was left running for sixty seconds with no output, which means
the stall is in the setup rather than in an assertion - most likely in the first
`GetVMAddress()` on a buffer created that way, since `buffer_reallocation_test`
never calls it and the tests that do (`compute_buffer_test`, `gpu_compact_unique_test`)
build their buffers through a `DeviceContext`, not bare.

Deleting it was the right call: a hanging test in the suite is worse than a missing
one, because it costs every future run rather than one reader's attention.

What the next attempt should try first: reproduce the setup from
`compute_buffer_test` rather than from `buffer_reallocation_test`, because the
former has a working `GetVMAddress()` call in it and the latter does not.










