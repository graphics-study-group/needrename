## Context

`radix_scatter.comp` claims output positions with `pos = atomicAdd(histogram.v[digit], 1u)`. That is a counting-sort scatter: it is correct at the bin level (every element with a digit lands in that digit's contiguous range) but it is **not stable** — the order inside a bin is the order the memory system grants the atomics, not the input order. LSD radix sort is only correct when every pass is stable, because each pass must preserve the ordering established by the previous one.

The failure is invisible for small inputs and fatal for the solver's. With keys below 2^20 (body indices) the high bytes are all zero, so the later passes put the entire array in one bin and re-permute it wholesale; the sort then returns an array that is *approximately* ascending. `SumByKey`'s merge guard (`s_key[tid] == s_key[partner]`) is equivalent to "same run" only on an ascending array, and a run's last element finalises with a plain store to `out[key]`, so a single inversion splits a body's run and the second partial overwrites the first — a silent loss of contributions.

Evidence gathered on this machine (RTX 3090):

| Experiment | Result |
|---|---|
| 6-layer brick scene, headless 60-step probe, new code | fails in 3–24 steps, max \|v\| 3.9 → 233 |
| same probe, pre-change float-atomic solver | passes 400 steps, max \|v\| → 0.07 |
| naive oracle reducer (one invocation per body, walking the same sorted array) | passes, max \|v\| ≤ 0.017 — the sorted array and the values are fine, the recursive reduce is not |
| reduce-input invariant check | `n=48 order_violations=1`, `e=31 prev_key=6 cur_key=0` — the input is not ascending |
| minimal repro with no solver involved (`gpu_radix_multiblock_test`, 400 elements / 22 small keys) | ~10 descending steps per trial |
| existing radix test (≤ 12 elements) | passes: a single warp's atomic grant order happens to equal input order |

Constraints that shape the approach:

- `shaderInt64` is an optional Vulkan feature, so a 64-bit key array is not available; records stay 32-bit.
- The repository's existing algorithm shaders contain no subgroup/group-non-uniform opcodes, so the ranking must not depend on `subgroupBallot`.
- `ParallelScan` already exists (arbitrary element count, in-place, caller-provided block-sums scratch) and is the house prefix-sum implementation.
- The house module pattern is: pImpl, `Rhi::DeviceContext &` constructor, geometry-free `Record` with per-call geometry, caller-provided buffers, static sizing helpers, and no GPU resource allocation in the constructor.
- `ShaderResourceBinding::BindBuffer` accepts `(offset, size)`, so one caller buffer can be partitioned into several shader bindings.

## Goals / Non-Goals

**Goals:**

- Make the radix pass genuinely stable, so a multi-pass LSD sort is correct for any input.
- Replace the `uvec2`-plus-mode contract with one that cannot hide the same bug: a key array plus an optional payload array, with the pass count derived from a key bound.
- Keep the solver's and the broad phase's semantics unchanged from the outside: `SumByKey` still reduces per body, the narrow phase still receives canonical `uvec2` pairs, and the fallback all-pairs path is untouched.
- Remove the work the sort cannot use (degenerate passes over all-zero key bytes).
- Restore the per-substep lagrange reset that the previous change dropped.

**Non-Goals:**

- Redesigning `CompactUnique`'s internals, `SumByKey`'s recursion, or the broad phase's grid.
- Multi-channel payloads: no caller needs them, and the solver's payload-as-index into a channel-major value buffer is strictly more general than carrying N floats.
- Fixing the adjacent latent defects recorded in the proposal (pair-count overflow, the shape-slot high-water mark, `max_global_shape_count` truncation, `ComputeStage`/binding lifetime).
- Supporting `shape_count > 65536` in the broad phase.

## Decisions

### D1: The record is a key array plus an optional payload array (SoA), and the mode enum disappears

The mode distinction was never about the pass count; it was the only way to say *which words of a `uvec2` are the key*. Once the key is its own array the ambiguity is gone, so `eFull`/`ePrimaryOnly` has nothing left to express. The payload array is optional because the broad phase's record *is* its key.

*Alternative rejected*: keep `uvec2` and replace the mode with a per-word bit bound. It works, but it parameterises the ambiguity instead of removing it, and the caller still has to reason about which word is compared.

*Alternative rejected*: a 64-bit key. `shaderInt64` is optional; `uvec2` needs no feature, and SoA removes the need for a two-word key entirely.

### D2: Stability comes from a transposed per-block histogram plus a deterministic rank

For an element with digit `d` in block `b`, warp `w`, lane `l`:

```
pos = scanned[d * num_blocks + b] + warp_offset(d, b, w) + rank_in_warp(d, w, l)
```

Writing each block's histogram **transposed** (`hist[digit * num_blocks + block]`) makes a single flat exclusive scan yield `bin base(d) + counts of d in earlier blocks` in one shot — no separate 256-bin scan. `warp_offset + rank_in_warp` is the element's rank inside its block, computed deterministically in shared memory (a per-warp count table plus a bounded comparison loop over a snapshot of the block's digits). Shared-memory atomics may build the *counts* (order-independent) but never a rank.

Because the histogram sub-step writes every cell of the transposed array exactly once, **no clear pass is needed** — one fewer dispatch per pass than the old scheme.

*Alternative rejected*: `subgroupBallot`-based match-any ranking (8 ballots, O(1) per element). It would be cheaper but introduces a subgroup-width assumption into a codebase whose shaders deliberately contain no subgroup opcodes.

*Alternative rejected*: decoupled look-back (single pass, no extra memory). It needs forward-progress guarantees between blocks that Vulkan does not provide.

*Verified*: the rank decomposition was brute-forced on the CPU (bijection onto `[0, count)`, stability, ascending output) for counts 1…5000, including counts that are not multiples of 256, capacities larger than the count, and blocks entirely beyond the count; and independently re-derived and simulated by a second reviewer. Scratch cost is `256 * ceil(capacity / 256)` `uint`s ≈ 4 bytes per record — roughly half of the old `uvec2` input.

### D3: The pass count is derived from the key bound, and the bound is verified on the host

```
num_passes = ceil(bit_width(max_key_value) / 8)
```

This removes the degenerate passes (for a 200-shape broad phase: 8 passes → 2; for the solver with ≤ 255 bodies: 4 passes → 1) and it is the mechanism that lets one module serve both call sites without a mode.

The shaders **do not clamp** and **do not mask**: clamping merges out-of-range keys into the top bin, where their mutual order is arbitrary and unrecoverable, which is exactly the class of silent corruption this change exists to remove. The contract is stated as "every key is below `2^(8 * num_passes)`" so that a key equal to the bound stays valid (this matters for the solver's sentinel, which equals `body_count`).

Verification is the caller's, on the host, because only the caller knows its key domain: the solver's keys are body indices or the sentinel `body_count` (both host-known), and the broad phase's keys are `a * shape_count + b` with both indices below `shape_count` (host-known). No GPU-side violation flag is added: it would be redundant with those assertions and would cost an optional buffer, a readback and a synchronisation point.

### D4: `Record` returns the buffers holding the result

With a derived pass count the count can be odd (one pass is the common solver case), so the result may land in either key array. Returning `{keys, payload}` keeps the ping-pong parity inside the module; exposing `NumPasses()` and letting callers pick the buffer would leak an internal invariant, and rounding up to an even count would waste a whole pass.

### D5: One opaque scratch buffer, partitioned internally; the module owns its `ParallelScan`

The caller passes one buffer sized by `GetRequiredScratchBytes(max_elem_capacity)`; the module splits it into the transposed histogram and the scan's block sums with offset bindings and rebuilds its internal `ParallelScan` when a call's capacity exceeds the capacity it was built for. The caller never learns that a scan exists, and **the module allocates no buffer** — the "algorithms take their buffers from outside" rule is respected; what the module owns is a pipeline object, exactly as it already owns its histogram/scatter stages.

*Alternative rejected*: sharing `SumByKey`'s record regions. The sizes are three orders of magnitude apart (solver: 10 KB of radix scratch vs 640 B of reduce records) and the broad phase has no reducer at all; sharing would only couple the two modules' internal layouts.

### D6: The digit width stays 8 bits

A wider digit (say 11 bits) would let a mid-sized key domain finish in one pass, but the transposed histogram is `bins * blocks` words, so it would grow 8× (4 MB → 32 MB at the broad phase's capacity cap) along with the scan. Two passes cost ~2 extra dispatches; the memory is not worth it.

### D7: The solver's invalid-slot sentinel becomes `body_count`

`kInvalidKey = 0xFFFFF` forces a 20-bit bound and therefore 3 passes. `body_count` is host-known, is larger than every real key, is still dropped by `SumByKey` (`key >= max_key_value`), and makes the bound tight. It also keeps the invariant that the entry count is a prefix of the sorted order: everything at or beyond it carries the sentinel.

### D8: The solver's entry passes write two arrays, and the payload is re-identified every substep

The payload is the entry's own slot id, and the sort permutes it **in place** — so a substep that skipped writing it would sort the previous substep's permuted order and gather values for the wrong entries. The entry pass therefore writes both arrays on every dispatch. This is the one trap in the solver-side adaptation that fails silently, so the contract is stated at the top of each entry shader.

### D9: `SumByKey`'s level-0 input changes minimally

`PairsIn` (`uvec2[]`) becomes `PayloadIn` (`uint[]`), the key comes from the `KeysIn` binding that already exists at every level, and the `gather_pairs` flag is kept (renamed `gather_payload`): the two paths still differ in where the value index comes from (the payload array at level 0, the element index deeper), so the flag is a real distinction, not an accident. This also removes the placeholder binding that pointed `KeysIn` at the pair buffer in gather mode.

*Alternative rejected*: unifying the two paths and deleting the flag. It is not obviously simpler and would force every reducer test to be rewritten in the same change.

### D10: The broad phase packs `a * shape_count + b` and unpacks after compaction

`a * shape_count + b` is injective over `a, b < shape_count` and order-preserving, so a plain `uint` key carries the pair's full identity and the sort needs no payload array (half the traffic of the old `uvec2` record). `CompactUnique` stays a generic algorithm over `uint` keys: `unpack_pairs.comp` restores the canonical `uvec2(a, b)` in the broad phase's own pass, and publishes `pair_count` from the unique count in the same dispatch, replacing the detector's separate count copy. The narrow phase and the fallback path keep their existing `uvec2` contract.

*Alternative rejected*: reconstructing inside `compact_scatter`. It would teach a `gpu_algorithm/` module collision-specific knowledge (`shape_count`, `a * n + b`), and the module already has a layering problem of this kind (it loads `memset_uint.comp` from the `collision/` directory).

*Alternative rejected*: letting the narrow phase consume packed keys. It would change the pair format across the whole pipeline and invalidate the spec's `pair.x < pair.y` invariant.

The bound `shape_count <= 65536` is asserted at `Configure`: at 65537 the packing would wrap *and* `shape_count * (shape_count - 1)` already wraps in three existing places, so the assertion makes an existing silent cliff loud rather than adding a new limit.

### D11: The lagrange clears are restored by loading the shaders that still exist

`clear_hinge_lagrange.comp` and `clear_fixed_lagrange.comp` are still in the tree and still listed in the shader inventory, but no C++ code loads them since the atomic-float removal. The pre-change solver dispatched both every substep; HEAD clears only the hinge axis, and does it with the integer-clear workaround, leaving `hinge_anchor`, `fixed_rotation` and `fixed_position` uncleared and accumulating across substeps. The fix is to load and dispatch the two existing shaders with each type's joint count, which also removes the workaround.

### D12: Implementation traps that must be respected

- No early `return` before a `barrier()` in either new shader: predicate the whole body, or the barrier is non-uniform (undefined behaviour) and the transposed histogram loses that invocation's cell.
- Inactive invocations contribute a sentinel digit so the rank loop and the per-warp counts agree on the active set; a mismatch lets two elements claim the same destination.
- The scan instance and its scratch must be sized `256 * ceil(capacity / 256)`, not `capacity` (`ParallelScan::Record` throws when the element count exceeds its construction-time maximum).
- Shared memory ≈ 9 KB (1 KB digit snapshot + 8 KB per-warp count table), comparable to `sum_by_key.comp`'s existing 9.2 KB.
- The transposed write-back is a stride-`num_blocks` scatter of 256 words per block. Accepted for now; a coalesced `[block][digit]` write plus a tiled transpose is the optimization if it shows up in a profile.

## Risks / Trade-offs

- **The key bound is a host promise.** A caller that under-states its bound gets a plausible but wrong order with no error. Mitigated by asserting at each call site, where the domain is host-known, and by stating the contract in the module's header.
- **`shape_count <= 65536` is a new hard failure** for a scene the engine could previously "run" (with silently wrapped buffer sizes). That is the intended trade: the failure is now loud and happens at configuration time.
- **Scratch memory grows with the capacity, not the count** (4 bytes per capacity slot). The broad phase's cap makes that 4 MB; the solver's capacities make it tens of KB.
- **A regression in `SumByKey`'s input contract would still be silent.** The reducer's failure mode is data loss, not an error. A debug-only ascending-order violation counter was considered and deferred (it needs a readback); the stability test is the current guard.
- **Process ordering.** `remove-float-atomics` and `xpbd-entry-count-driven-reduce` are unarchived and already carry `gpu-radix-sort` (primary-only mode, geometry-free construction) and `gpu-sum-by-key` deltas that this change supersedes. They must be archived first, or their conflicting deltas dropped, or the two MODIFIED blocks will collide at archive time.
- **Carried drift.** `gpu-compact-unique`'s Purpose paragraph still describes `uvec2` pairs, and its remaining requirements still describe the `RenderSystem&`/`AddPasses`/`RenderGraphBuilder` API; `physics-gpu-shaders`' solver shader list still omits the `entries/*` shaders added by the previous change. This change fixes the requirements it touches and leaves the rest as noted drift rather than silently rewriting them.
- **The fallback all-pairs path emits unordered pairs** (its slots come from `atomicAdd`), unlike the sorted spatial-hash path. Consumers do not depend on the order, so this is recorded, not changed.
