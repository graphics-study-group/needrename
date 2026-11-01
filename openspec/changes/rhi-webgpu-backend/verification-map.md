# Which scenario is verified, and how

The change makes 75 promises across seven delta specs. This file maps each one to
the evidence that exists today, and says plainly which have none.

Three kinds of evidence appear below, and they are not interchangeable:

- **test** — a named entry in the suite; `ctest --preset msvc-debug -R <name>`. A few entries are the same binary with a flag rather than a separate source file: `compute_kernel_oversize_test` is `compute_kernel_test --oversize-push`, which is why no `*oversize*` executable exists.
- **build** — the claim is enforced by something that fails to compile or link.
- **none yet** — nothing checks it. A promise without evidence is a wish, and this
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
| Draws are confined to the render scope | **none yet** |
| Dispatch is confined outside the render scope | **none yet** |
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
| Address is stable while the window is open | **none yet** |
| Address is invalidated by device use | **none yet** |
| Explicit flush publishes writes | partially: `model_matrices_initialization_test` writes through the buffer |
| Implicit flush covers forgotten calls | **none yet** |
| Vulkan keeps the window open | **none yet** |
| Refresh exposes completed device writes | **test**: `gpu_buffer_retirement_test` |
| No torn state is observable | **none yet** |
| Callback fires on completion | **test**: `mrt_test` (its readback callback runs) |
| Registration does not block | **none yet** |
| Test reads results synchronously | **test**: `mrt_test`, `headless_offscreen_test` |
| Product path never blocks | **none yet** |
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
| `rhi-command-encoder` | 14 | 10 | 4 |
| `rhi-compute-kernel` | 6 | 6 | 0 |
| `rhi-mapped-window` | 12 | 5 | 7 |
| `submission-helper-sync` | 7 | 7 | 0 |
| `shader-wgsl-artifact` | 9 | 0 | 9 |
| `webgpu-backend` | 17 | 0 | 17 |
| **Total** | **75** | **37** | **38** |

## The two kinds of "not verified"

They are worth separating, because they call for different work.

**Blocked on an unbuilt backend (30).** Everything in `shader-wgsl-artifact` and
`webgpu-backend`, plus the five cross-backend rows in `rhi-command-encoder`. Nothing
can check these until group 7 and group 8 exist; writing a test now would test
nothing.

**Verifiable today but not written (8).** These are the ones that should bother a
reader:

- *Draws are confined to the render scope*, *Dispatch is confined outside it* - the
  same shape: the seam states a rule it does not enforce.
- *Web build contains no runtime shader compiler* - checkable by scanning the wasm
  library's imports for glslang and SPIRV-Cross once the library is non-trivial.
- Four in `rhi-mapped-window` (address stability, invalidation, implicit flush,
  registration not blocking) - each is a small focused test over an API that exists.

That second group is what a reviewer should look at first: it is the difference
between "the design is not finished" and "the design says something the code does
not do".





