# Remove Float Atomics

## Why

The XPBD GPU solver gates physical-device selection on `VK_EXT_shader_atomic_float` (`shaderBufferFloat32AtomicAdd`), which is used for nothing except per-body Jacobi delta accumulation — and which many GPUs do not support, rejecting them outright. A CAS-loop (`atomicCompSwap`) replacement was prototyped and measured: its retry contention on hot bodies is unacceptable. Integer atomics are core Vulkan and need no extension, so the solver will move to a fully atomic-free accumulation path: conflict-free scatter + radix sort by body + recursive segmented reduction (`SumByKey`), letting the engine drop the extension entirely and run on any Vulkan 1.3 device.

## What Changes

- **Device initialization** (`engine/Rhi/Device/DeviceInterface.cpp`): remove `VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME` from `DEVICE_EXTENSION_NAMES`, remove the `shaderBufferFloat32AtomicAdd` suitability check, and remove the `vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT` pNext chain from device creation. The device floor becomes Vulkan 1.3 core features (`dynamicRendering`, `synchronization2`) plus `timelineSemaphore` — unchanged otherwise. This is a compatibility expansion, not a breaking change.
- **New GPU algorithm `SumByKey`** (`engine/Physics/gpu_algorithm/`): recursive segmented reduction over sorted `(key, value)` arrays. One shader reused at every recursion level: 256-entry blocks do an in-shared-memory same-key doubling reduction and emit at most 2 boundary records per block into the next level; the final single-block level writes per-key sums directly. Supports `N` float value channels batched into one buffer (channel-major layout, `N` passed as a push constant), so the solver's 7 channels (Δlin xyz, Δang xyz, contribution flag) reduce in one pass per level. Zero atomics, entry-parallel, depth ≈ log₁₂₈(E/256) (≤ 4 dispatches for any realistic size). Caller-provided buffers, `GetRequiredRecordsBytes(max_entries, num_channels)` static sizing, matching the `ParallelScan`/`RadixSort` house pattern.
- **`RadixSort` gains a primary-key-only mode**: 4 radix passes sorting by `.x` with `.y` riding along as opaque payload (the existing 8-pass mode stays). Halves the sort dispatch count for the solver's body-key sort, where the payload (entry slot id) needs no ordering.
- **XPBD solver pipeline rework** (per substep, after collision detection) — the accumulation mechanism changes, the shader shape deliberately does not:
  1. Three new **entry passes** (`entries/contact_entries.comp`, `entries/hinge_entries.comp`, `entries/fixed_entries.comp`) write entry pairs `(owner_body_or_INVALID, slot)` into the sort buffer. They are dispatched over the entry *capacity* and own exactly one contract: write every slot. They read no body state — every "should this constraint contribute?" check stays in the accumulate shaders, where it was before this change.
  2. `RadixSort` (primary-only) sorts entries by body — once per substep for contacts *and* for hinge/fixed (nothing is cached across substeps, matching the pre-change solver, which had no caching).
  3. New `invert_permutation.comp` pass: `pos_of[slot] = sorted_position` plus `sorted_keys[p] = key` (one pass, two outputs).
  4. Per iteration, `dispatch_clear` zeroes the scratch, then `accumulate_*` shaders run: they compute per-constraint deltas exactly as before and scatter the 7 channels through `pos_of` into a channel-major scratch buffer — pure writes, zero atomics, and no in-shader clearing.
  5. `SumByKey::Record` reduces the sorted scratch to per-body partials (one instance per constraint type: contact, hinge, fixed; velocity reuses the contact instance's permutation with its own scratch).
  6. `apply_*` shaders merge the three partial sets, average by total count, apply, and zero the outputs they consumed. The three per-body output buffers are cleared once before the substep loop, as the pre-change solver cleared its delta accumulators there.
- **No `mode` flag anywhere**: entry construction and delta accumulation are separate shaders with separate contracts, dispatch domains, frequencies and resource sets, instead of two branches of one shader. `common/xpbd_entry_validity.glsl` — the shared side-validity predicate that the two branches had to apply identically — is deleted, since an unwritten slot is now inert regardless of its key.
- **Float atomics removed from all four accumulate shaders** (`accumulate_contact_position/velocity`, `accumulate_hinge_position`, `accumulate_fixed_position`): the `GL_EXT_shader_atomic_float` extension line and every float `atomicAdd` disappear. Each shader otherwise keeps its pre-change text — same guards, same control flow, same math — with each `atomicAdd` block replaced by a single `scatter_slot(slot, lin, ang)` call. `lagrange` accumulation becomes a plain `+=` (one thread per contact/joint slot — no contention). `delta_count`/`vel_delta_count` buffers are deleted; the count is the `SumByKey` sum of the flag channel (identical semantics: counts actual per-iteration contributions only).
- **Physics behavior preserved**: the Jacobi average `ΣΔ / count` and the per-constraint math are unchanged; only the accumulation mechanism changes.

## Capabilities

### New Capabilities

- `gpu-sum-by-key`: the `SumByKey` GPU algorithm — recursive segmented reduction over sorted key-value arrays, N-channel batching, record-buffer sizing and level structure, INVALID/DEAD key semantics, and the caller-provided-buffer API contract.

### Modified Capabilities

- `xpbd-contact-solve`: the Jacobi accumulation requirement changes from float `atomicAdd` (via `GL_EXT_shader_atomic_float`) to scatter + `SumByKey` reduction; accumulator reset semantics move to explicit `dispatch_clear` passes plus the zeroing of the per-body outputs; entry construction becomes its own pass, separate from delta accumulation; the velocity solver shares the contact permutation.
- `gpu-radix-sort`: new primary-key-only 4-pass sort mode with payload ride-along.
- `physics-gpu-shaders`: the XPBD solver's loaded-shader inventory gains the three `entries/*.comp` passes, `invert_permutation.comp` and the extra `clear_int_buffer.comp` dispatch sites; the `SumByKey` reduce shader is algorithm-owned (loaded via the `SumByKey` class, like `RadixSort`'s shaders).

## Impact

- `engine/Rhi/Device/DeviceInterface.cpp` — extension list, suitability check, feature chain (3 sites).
- `engine/Physics/gpu_algorithm/SumByKey.h/.cpp` (new) + `engine/Physics/shader/algorithm/sum_by_key.comp` (new, the recursive block shader).
- `engine/Physics/gpu_algorithm/RadixSort.h/.cpp` + its radix shaders (primary-only mode; shader files unchanged, dispatch loop changes).
- `engine/Physics/shader/solver/XPBDSolver/entries/{contact,hinge,fixed}_entries.comp` (new) — entry-list construction, one pass per constraint type.
- `engine/Physics/shader/solver/XPBDSolver/accumulate_{contact_position,contact_velocity,hinge_position,fixed_position}.comp` — restored to their pre-change text with `atomicAdd` replaced by `scatter_slot`, `mode` field removed, extension removed.
- `engine/Physics/shader/solver/XPBDSolver/apply_body_{position,velocity}_deltas.comp` — read the merged partial sets, divide by the summed flag channel, clear what they consumed.
- `engine/Physics/shader/solver/XPBDSolver/invert_permutation.comp` (new).
- `engine/Physics/shader/solver/XPBDSolver/common/xpbd_entry_layout.glsl` (new: channel/slot conventions) — `common/xpbd_entry_validity.glsl` is deleted.
- `engine/Physics/Solver/XPBDGpuSolver.h/.cpp` — buffer allocation (entry pairs ping/pong, `pos_of`, sorted keys, channel-major scratch, records, per-body outputs), pipeline wiring (entry → sort → invert per substep for all three types, clear → accumulate → reduce → apply per iteration), `SumByKey` instances, and removal of the hinge/fixed entry caches.
- Tests: shader compile pipeline picks up new `.comp` files via the existing glob; unit/integration coverage for `SumByKey` and the reworked solver path (see tasks).
- No public API breaks; all affected buffers and shaders are solver-internal. Device compatibility expands (any Vulkan 1.3 device).
