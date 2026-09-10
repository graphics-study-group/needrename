# Design: Remove Float Atomics

## Context

The XPBD GPU solver (see proposal.md — Why) currently accumulates per-body Jacobi deltas with float `atomicAdd` in four shaders, which requires `VK_EXT_shader_atomic_float` (`shaderBufferFloat32AtomicAdd`). All other atomics in the physics pipeline are integer and core Vulkan. The codebase already provides the reusable building blocks this design leans on: `RadixSort` (KV sort of `uvec2` pairs, caller-provided buffers, `Record(cb, ...)` API), `ParallelScan` (multi-level recursive scan over one partitioned scratch buffer), and the solver's direct-dispatch `GPUStep` with manual `pipelineBarrier2` between passes.

The reference semantics that must be preserved: per iteration, `applied = Σ(contributions) / count`, where `count` counts only the constraints that actually contributed in that iteration (the current `delta_count`, incremented only after the `C > 0`/`denom > ε` gates).

The reference *shape* must be preserved too. The pre-change solver has a clean rhythm that the accumulation mechanism must not disturb:

```
position loop:  accumulate(contact/hinge/fixed) → apply (average → apply → reset own accumulators)
velocity loop:  clear counters → accumulate → apply
```

The accumulate shaders own every "should this constraint contribute?" decision; clearing lives outside them; `apply` resets what it consumed. This design keeps that division of labour and keeps the accumulate shaders as close to their pre-change text as the mechanism allows — the only intended textual change is `atomicAdd(delta[body], v)` becoming one `scatter_slot(slot, lin, ang)` call.

## Goals / Non-Goals

**Goals:**

- Remove every dependency on `GL_EXT_shader_atomic_float` and the `VK_EXT_shader_atomic_float` device feature (device floor = Vulkan 1.3 core).
- Preserve the Jacobi solve semantics bit-for-bit at the level of "which contributions are counted" (sums may round differently — accumulation order changes).
- Keep the accumulate shaders recognisably identical to their pre-change versions: same guards, same math, same control flow, no `mode` flag, no in-shader clearing.
- Keep clearing explicit and idiomatic (`dispatch_clear`), as the pre-change solver did.
- Ship a reusable, house-style `SumByKey` GPU algorithm in `gpu_algorithm/` (pImpl, caller-provided buffers, static sizing helpers, `Record` with internal barriers).
- Zero same-address atomic contention on the hot per-iteration path.

**Non-Goals:**

- Bit-identical cross-run determinism (accepted: the radix scatter is unstable, so within-key order is unspecified; see Risks).
- Stable (rank-based) radix scatter.
- Merging the three constraint-type reductions into a single dispatch (left as a documented optimization knob).
- Gauss-Seidel or any change to the constraint math itself.

## Decisions

### Decision 1: Scatter + sort + recursive segmented reduction (no atomics)

**Choice**: Per substep, build `(body, slot)` entry pairs, sort by body with `RadixSort` (primary-key mode), invert the permutation once, then per iteration scatter per-constraint contributions through the permutation into a per-channel scratch array and reduce per body with the new `SumByKey` algorithm.

**Alternatives rejected**:

- *CAS loop on int-typed floats* (`atomicCompSwap` retry): prototyped and measured elsewhere — retry contention on hot bodies is unacceptable. Rejected by the user.
- *Per-body workgroup reduce*: one WG per body is load-imbalanced (most bodies have 0–8 entries; one ground body can have tens of thousands — serial strided rounds).
- *Int-atomic slot reservation per iteration* (`atomicAdd(body_counter[body], 1)` to place values): reintroduces the same-address hot spot the whole change exists to remove; also still non-deterministic within segments.

### Decision 2: Entry model — `(body, slot)` pairs; the key is slot *ownership* only

**Choice**: Entry slot `e = cidx * 2 + side` for contacts (each contact point contributes at most one count per body side). Hinge/fixed use **4 slots per joint = 2 bodies × 2 constraints**: each body may receive one contribution per scalar constraint the joint solves. A hinge solves the aligned-axis constraint (angular-only) and the anchor-point constraint (linear + angular); a fixed joint solves rotation and position. Every distinct (joint, constraint, body) target is its own slot, so each carries an independent presence (its own flag) and its own delta channels; the `SumByKey` reduction sums the per-body partials. This preserves the old `delta_count` semantics exactly (a body is counted once per contributing scalar constraint — up to 2 per joint, not 3; splitting anchor-linear vs anchor-angular apart would have changed the Jacobi denominator). Slot ids are the identity payload (`.y`) so the sort permutes them for free.

The key encodes **ownership, not body state**: it is the index of the body the slot belongs to, or `INVALID = 0xFFFFF` (sorts last, below the `2^20` radix limit) when the slot has no owner this substep — no such contact, no such joint, out-of-range owner index. The entry pass therefore reads **no body state at all**: no `alive`, no `is_kinematic`, no `mass`, no `joint_alive`. Every one of those checks stays exactly where it was before this change — inside the accumulate shaders, gating the writes.

**Why a superset**: whether a constraint actually contributes is per-iteration dynamic (`C > 0`, `denom > ε` depend on current poses), so the sorted structure must contain a superset of contributors. The per-iteration contribute decision is carried by a flag channel instead of changing the keys. Because every scratch slot is zeroed before each accumulation (Decision 8), a slot that is never written contributes 0 to the sum and 0 to the flag total, so an owned-but-silent slot (kinematic body, zero inverse mass, gated-off constraint) has exactly the same effect on `applied = Σ / Σflag` as an absent one — which is the old `delta_count` behaviour.

**Invariant (correctness core)**: the key set always ⊇ the contribution set. It is now enforced by three independent, locally-checkable facts rather than by keeping two code paths in sync: (1) the entry pass writes `INVALID` for every unowned slot; (2) the scratch is zeroed every iteration, so an unwritten slot is inert regardless of its key; (3) `SumByKey` drops keys ≥ `max_key_value`.

**Superseded**: an earlier revision of this design kept a shared side-validity predicate (`common/xpbd_entry_validity.glsl`, `xpbd_side_static_valid(alive, kinematic, mass)`) that both an entry mode and an accumulate mode had to apply identically, called the "no-drift invariant". With per-iteration clears and the pre-change guards restored in the accumulate shaders, the predicate is redundant — a wrong key on a slot that is never written is harmless — so it and its include file are deleted. This removes a whole class of stale-key bugs: the entry list no longer depends on body state, so body alive/kinematic/mass changes cannot invalidate it.

### Decision 3: Contribution count = a flag channel inside the same reduction

**Choice**: Channel 6 of the scatter carries 0.0/1.0 (skipped/contributed); the per-body count is the `SumByKey` sum of that channel.

**Rejected**: count = segment length (wrong — segments are the static superset, not actual contributors); per-body int `atomicAdd` counters (hot spot); inferring "contributed" from nonzero deltas (fragile — a delta can round to 0.0). The flag is exactly the current `delta_count` semantics, relocated into the reduction.

### Decision 4: Seven channels batched into one buffer per group

**Choice**: `SumByKey` takes 1 key array + 1 packed value buffer of `N` float channels in channel-major layout (`value(c, i) = buf[c * stride + i]`, `stride` = current level's element count), `N` as a push constant (`MAX_CHANNELS = 8` compile-time shared-memory capacity). Solver channels: 0..2 Δlin xyz, 3..5 Δang xyz, 6 flag.

**Rejected**: `N` separate SSBO bindings per channel — the reduce shader would need 23 SSBOs (keys + 7 values + 7 record values + 7 outputs), exceeding the guaranteed `maxPerStageDescriptorStorageBuffers = 8` minimum, and 7 independent single-value calls would multiply the per-level dispatches by 7. Packing keeps the reduce at **5 SSBOs** (`KeysIn`, `ValsIn`, `KeysRec`, `ValsRec`, `OutVals`) and one pass per level for all channels. `N` cannot be a specialization constant: `ComputeStage::Instantiate` accepts only SPIR-V + name, so push constants avoid any Rhi change.

### Decision 5: RadixSort primary-only mode (4 passes)

**Choice**: A mode sorting by `.x` only, `.y` riding along as opaque payload (the scatter writes whole pairs). Saves 12 dispatches per substep vs. the 8-pass mode; since the within-key order is unspecified anyway (unstable scatter), pre-sorting by `.y` buys nothing.

### Decision 6: Inversion pass produces the uniform key array

**Choice**: New solver shader `invert_permutation.comp`: one thread per sorted position `p` writes `pos_of[pairs.y] = p` (bijection — conflict-free) and `sorted_keys[p] = pairs.x`. This gives `SumByKey` a plain `uint[]` key array at every level (level 0 included) instead of teaching it about the sort's `uvec2` layout.

### Decision 7: SumByKey — one shader, recursive levels, records partitioned like ParallelScan

**Choice**: A single `sum_by_key.comp` runs every level. Per block (256 entries, 256 threads, ~9 KB shared):

1. Cooperative load of key + `N` channel values (out-of-range → `INVALID` key, 0.0 values).
2. Same-key pairwise doubling in shared memory using the Blelloch upsweep pairing `idx = (tid+1)*2*offset - 1` (the exact `radix_prefix_sum_256` index formula) with the merge condition `s_key[idx-offset] != DEAD && s_key[idx-offset] == s_key[idx]`; the right element is dead-marked (`DEAD = 0xFFFFFFFE`). Disjoint pairing avoids the shared-memory race of naive doubling.
3. An aliveness suffix scan (one extra shared array, 8 rounds) identifies segment heads; position 0 is always alive and the last alive head is the last segment's head.
4. Settlement: segments fully contained in the block write `out[key]` directly (unique writer — one key has exactly one global segment); the first segment's partial goes to record slot `2b`, the last segment's to `2b+1`; when the whole block is one segment, `2b+1` receives an all-zero record with the same key (chain stays contiguous, sums unaffected, exact in FP). Keys ≥ `max_key_value` (INVALID) are guarded off.

Level structure: `R_0 = max_entries`, `R_{i+1} = 2 * ceil(R_i / 256)` until `R_k ≤ 256`. Records live in one caller-provided buffer partitioned by level (offsets fixed at construction — the `ParallelScan::GetRequiredBlockSumsBytes` pattern). The final level (`num_blocks == 1`, detected in-shader — no mode flag) writes everything directly; nothing needs "adding back" because sums, unlike scans, close at the leaf level. Depth ≤ 4 for any realistic size; a single key spanning more than 512×512 entries is just a longer record chain absorbed by the same code.

**Rejected**: a second serial "record-chain" pass — it serialized per-run chains in one workgroup; the recursion keeps every level entry-parallel.

### Decision 8: Per-body output handling and explicit clears

**Choice**: Each `SumByKey` instance (contact / hinge / fixed) writes its own
`7 × N_body` output buffer in **channel-major layout** with stride
`body_count` (`out(c, body) = out[c*body_count + body]`); channel 6 holds the
contribution flag whose per-body sum is that body's contribution count.
`apply_*` merges the three partial sets (position) or the single velocity set,
reads the count from each set's channel 6, and divides the merged deltas by the
total count.

Clearing is explicit, separate from accumulation, and uses the existing
`clear_int_buffer.comp` dispatch — the pre-change solver's idiom (it already
zeroed float buffers such as `ContactLagrange` through that shader):

- The three per-body output buffers are zeroed **once before the substep loop**,
  which is exactly where the pre-change solver cleared its per-body delta
  accumulators and `delta_count`. `apply_*` clears every per-type cell it
  consumes as it goes, so a body with no entries keeps zeros and the buffers are
  ready for the next iteration. This also removes a latent uninitialised read:
  `SumByKey` writes only keys present in its input, so a body absent from a given
  constraint type is never written by the reduction.
- Each per-slot scratch buffer is zeroed **once per iteration, immediately before
  its accumulate pass** (`num_channels * entry_capacity` elements). `SumByKey`
  reduces a fixed-capacity input, so every slot must carry a meaningful value
  every iteration; the clear supplies the zeros.

**Rejected**: in-place zeroing inside the accumulate shaders (the earlier
`zero_slot` else-branches). It saves one dispatch per iteration, but forces every
accumulate shader to carry "contributed / did not contribute" branching that the
pre-change shaders never had — turning the two write sites of each constraint
into four — and it makes the safety of an unwritten slot depend on a cross-file
chain (entry pass writes `INVALID` → `SumByKey` drops keys ≥ `max_key_value`).
The explicit clear is the pre-change idiom and keeps each accumulate shader
literally "the old shader with the atomic replaced by a scatter call".

### Decision 9: Pipeline wiring — everything rebuilt every substep, nothing cached

**Choice** (per substep, inside `GPUStep`):

```
collision detection (existing) → barrier
clear the three per-body output buffers                    [1 dispatch each]
build contact entries (full capacity) → sort → invert
build hinge entries (4 × count)       → sort → invert      [when hinge_joint_count > 0]
build fixed entries (4 × count)       → sort → invert      [when fixed_joint_count > 0]
per position iteration:
    clear contact scratch → accumulate_contact_position → SumByKey(contact)
    clear hinge scratch   → accumulate_hinge_position   → SumByKey(hinge)
    clear fixed scratch   → accumulate_fixed_position   → SumByKey(fixed)
    apply_body_position_deltas
per velocity iteration:
    clear velocity scratch → accumulate_contact_velocity → SumByKey(reuses contact permutation)
    apply_body_velocity_deltas
```

Every entry list — contact, hinge **and** fixed — is rebuilt, sorted and inverted
once per substep, and all position/velocity iterations of that substep reuse the
same permutations. Nothing is cached across substeps.

**Why no caching**: the pre-change solver had none, and any cache keyed on counts
is unsound here — the entry key follows the body the slot belongs to, and while
the *owner index* is stable, a caching scheme also has to survive body
alive/kinematic/mass changes and joint array content changes that leave counts
untouched. Rebuilding costs one entry dispatch plus 13 sort/invert dispatches per
joint type per substep, all proportional to `4 × joint_count`.

**Resource lifetime lives in `PreGPUStep`**: every capacity-dependent resource — the three reduce groups' buffers, the per-type lagrange buffers, the `RadixSort`/`SumByKey` instances and the constant `pair_count` — is (re)sized in `PreGPUStep`, the phase the solver's own header reserves for "buffer sizing, CPU uploads" and the one that runs outside the command-buffer scope. `GPUStep` only derives dispatch geometry and records. Buffers size-check themselves by byte count (`EnsureBuffer`); `RadixSort`/`SumByKey` take their capacity as a construction-time parameter and cannot be resized in place, so `EnsureSortAndSum` asks the objects (`GetMaxElemCount`, `GetMaxEntries`, `GetMaxKeyValue`) whether they still match the current geometry and rebuilds them otherwise. The solver keeps no shadow copy of geometry the resources already know — an earlier revision did, and its contact-group copy was never refreshed when `max_contact_point` (which tracks the shape count) changed: that either threw out of `RadixSort::Record` on growth or silently reduced with the wrong channel stride on shrinkage. One `EnsureSortAndSum` rule now covers all three constraint types.

### Decision 10: Device feature removal

**Choice**: In `DeviceInterface.cpp`, drop `VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME` from `DEVICE_EXTENSION_NAMES`, drop the `shaderBufferFloat32AtomicAdd` requirement from `GetPhysicalDeviceSuitabilityScore`, and remove the `vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT` node from the `CreateDevice` pNext chain. Nothing else changes; `DEVICE_EXTENSION_NAMES` shrinks 4 → 3.

### Decision 11: Entry construction is its own shader, not a `mode` of the accumulate shader

**Choice**: Each constraint type gets an **entry pass** shader — `entries/contact_entries.comp`, `entries/hinge_entries.comp`, `entries/fixed_entries.comp` — dispatched over the entry *capacity* (one thread per contact point / per joint, writing 2 / 4 slots). Its whole contract is: write every slot in `[0, capacity)`; unowned slots get `INVALID`; never return before writing. Because the dispatch rounds up to whole workgroups, a thread whose last slot is `>= capacity` owns no slots and returns immediately (the entry passes carry `entry_capacity` in their push block for exactly this bound). The accumulate shaders keep only the per-iteration job, with the pre-change guard structure verbatim.

**Rejected**: one shader per type with a `mode` push-constant selecting entry-building vs accumulation. It reads as a single abstraction, but the two jobs differ in nearly every dimension:

| | entry pass | accumulate pass |
|---|---|---|
| dispatch domain | entry capacity | live contact / joint count |
| frequency | once per substep | once per iteration (×20) |
| storage buffers bound | 3–5 | 12–17 |
| write contract | write **everything** | write **only what contributes** |
| reads | ids, shape→body map | poses, inertia, normals, contact points, lagrange |

The `mode` field was already leaky in practice: `accumulate_contact_velocity.comp` only ever needed the accumulate half, so it never had a `mode` field at all. Worse, in `accumulate_hinge_position.comp` / `accumulate_fixed_position.comp` the pre-existing `joint_alive` and body-index guards sat **before** the `mode` branch, so the entry half never wrote the slots of a dead or out-of-range joint. That silently broke the full-capacity contract: `pos_of` was built from stale pairs, and a joint disabled between rebuilds left its slots keyed to live bodies with frozen scratch values, which `SumByKey` then credited to those bodies every iteration. Separate shaders make that entire class of mistake unrepresentable — the entry pass has no body-state guards to misplace — and the contract of each file fits in one sentence at the top of the file.

Splitting also shrinks each pass: the contact entry pass binds 5 storage buffers instead of 18, and the joint entry passes bind 3.

## Risks / Trade-offs

- *[Per-substep overhead: 3 entry dispatches + 3 sorts (36 dispatches) + 3 permutations + 3 output clears, plus 3 scratch clears, 3 accumulate passes, 3 reductions and 1 apply per position iteration]* → ~400 dispatches/substep vs. ~240 for the cached/mode-based variant and ~130 pre-change — accepted for the compatibility win and the restored shader shape. The added clears are pure memsets (`7 × capacity` floats each, ~229 KB per contact clear at `max_contact_point = 4096`; 20 iterations × 8 substeps ≈ 37 MB/frame, i.e. a few percent of a single frame's bandwidth budget) and the joint entry/sort/invert work is proportional to `4 × joint_count`. The reduction remains the dominant cost. Optional later: merge the three reduces into one dispatch, or batch the three scratches into one buffer to clear them in a single dispatch.
- *[Scatter writes (7 channels × E) are less cache-friendly than in-place atomics]* → Writes go through L2; the reduce reads are fully linear. The exchange for zero contention is the point of the design.
- *[Non-deterministic within-key order (unstable radix scatter) → last-ulp FP variation in sums, same noise class as today's atomics]* → Accepted by the user. Deterministic-set guarantee holds; a stable rank-based scatter is a possible future upgrade if bit-identical reproducibility is ever needed.
- *[Packed channel-major layout is a cross-shader convention (accumulate writes it, SumByKey reads it, apply consumes outputs)]* → Single constant definition (`kNumChannels = 7`, channel order) shared by the solver's C++ and shader includes; any mismatch shows up immediately as wrong physics in tests.
- *[`MAX_CHANNELS = 8` hard-caps channels]* → Solver needs 7; raising the cap only costs shared memory (16 KB budget allows ~14).
- *[Solver dispatch code grows]* → The `SumByKey` class encapsulates the recursive dispatch; the solver treats it as one call per iteration, mirroring the `RadixSort` call sites. Splitting entry from accumulate removes the `mode` field from every push-constant block and deletes the hinge/fixed dirty-cache machinery, so the solver C++ is simpler than the mode-based variant despite the extra passes.

## Migration Plan

1. Land `SumByKey` + its shader + unit tests first (pure addition, no behavior change elsewhere).
2. Add the `RadixSort` primary-only mode + tests (additive).
3. Rework the solver shaders: restore each accumulate shader to its pre-change text with `atomicAdd` replaced by `scatter_slot`, add the three entry-pass shaders, add `invert_permutation.comp`, rework the two `apply_*` shaders for the merged partial sets. Rework `XPBDGpuSolver` buffers and pipeline wiring, delete the hinge/fixed entry caches, add the explicit clears. Verify existing physics scenarios (box stack, resting contact, hinge chain).
4. Remove the device feature/extension sites in `DeviceInterface.cpp` last, after the solver no longer emits float atomics.
5. Rollback: revert the change; no data-format or serialization impact exists (all buffers are solver-internal).

## Open Questions

None blocking. Deferred knobs that do not change the specs or task breakdown: merging the three per-iteration reduces into one dispatch (requires a single reduce group, which conflicts with per-type construction-time capacities unless the solver gains joint-capacity config); merging the three scratches into one offset-addressed buffer so a single clear dispatch covers all of them; moving `N` from a push constant to a specialization constant if `ComputeStage` ever gains specialization support.

## Addendum: SumByKey input-length model (Scheme A)

`SumByKey` keeps the fixed-capacity contract of the ADDED spec unchanged: level-0
geometry, record regions, and the per-call dispatch count are all derived from the
construction-time `max_entries`, and `Record` takes no runtime count.  This is the
radix-sort family model (input produced on the GPU, count unknown on the CPU),
*not* the `ParallelScan` model (CPU-known count, dynamic geometry): the XPBD
contact count is produced by GPU collision detection and never read back, so a
CPU-count `Record` is not usable on the contact path.

To keep the reduce input fully meaningful every call, the solver guarantees the
sorted key array `[0, capacity)` is always valid each substep:
- Contact entries are **fully written every substep**: the contact entry pass is
  dispatched over the fixed contact capacity and writes `(key, slot)` for both
  sides of every contact slot, using `INVALID` keys wherever a contact/body is
  absent.  Consequently the radix `pair_count` equals the fixed capacity (a
  constant, no dynamic count buffer), the whole entry list is sorted each substep,
  and `invert_permutation` is a pure permutation over the full capacity.
- Hinge/fixed entries are exactly `4 * joint_count` (2 bodies × 2 scalar
  constraints per joint), fully written every substep by their entry pass, with
  `INVALID` for slots whose joint does not exist or whose owner index is out of
  range.  Their `SumByKey` capacity equals that count; the buffer objects and
  `RadixSort`/`SumByKey` instances are (re)created when the joint count changes,
  while the entry list itself is rebuilt every substep (Decision 9).
- When a joint count is zero, the capacity is clamped to a minimum of 4 (one
  joint's worth) and the entry pass is still dispatched with at least one
  workgroup so all four slots are written `INVALID` — the entry pass is dispatched
  over capacity, never over the live count, so it can never leave a slot
  unwritten.

Consequence: `SumByKey` never reads a count buffer; record regions are fully
populated every call (2 records per block), so level-1..k inputs stay dense.
The per-substep contact entry list is always `2 * max_contact_point` slots
(fixed capacity); writing INVALID for empty slots avoids stale-key pollution
and removes the need for a separate pair-count/tail-fill plumbing.
