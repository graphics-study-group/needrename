## Why

The GPU XPBD solver's stability rests on the radix sort handing `SumByKey` a genuinely key-ascending array, and it does not. `radix_scatter.comp` claims output positions with `pos = atomicAdd(histogram.v[digit], 1u)`: that is a counting-sort scatter, correct per bin but **not stable**. LSD radix sort is only correct if every pass is stable, so with solver-scale keys (body indices < 2^20, whose high bytes are all zero) the later passes put the entire array in one bin and re-permute it in atomic-grant order. The array comes out *approximately* ascending.

`SumByKey`'s merge guard (`s_key[tid] == s_key[partner]`) is equivalent to "same run" only on an ascending array, and each run's last element finalises with a plain store to `out[key]` — so a single inversion splits one body's run in two and the second partial **overwrites** the first, silently dropping contributions. Measured on the 6-layer brick scene: the pre-change float-atomic solver passes 400 steps (max |v| → 0.07), the current one blows up in 3–24 steps; a 60-step headless probe fails with max |v| ≈ 233. The bug is in the sort, but it is only *reachable* because the sort's contract let it hide: an `eFull`/`ePrimaryOnly` mode over a `uvec2` record never says plainly which words are the key, and the only test exercised ≤ 12 elements, where a single warp's atomic grant order happens to equal input order.

The same pass-count arithmetic also means the sort does work it cannot use: 8 passes for keys bounded by a few hundred, 2 of them degenerate whole-array permutations.

## What Changes

- **BREAKING** — `RadixSort`'s contract becomes one `uint` key array plus an optional `uint` payload array (SoA), sorted **stably** ascending. `RadixSortMode`, the `max_shape_count` validation and the fixed 4/8-pass counts are removed: the pass count is derived from `max_key_value`, and `Record` returns the buffer holding the result because the pass count can now be odd (1 pass for ≤ 256 keys).
- The scatter becomes genuinely stable: a new `radix_block_histogram.comp` writes each block's 256-bin histogram **transposed** (`hist[digit * num_blocks + block]`, every cell written exactly once, so no clear pass), the existing `ParallelScan` exclusive-scans that array — which yields `bin base + earlier-block offset` in one scan — and `radix_scatter.comp` computes a deterministic in-shared-memory rank. No atomic decides element order. `radix_histogram.comp` and `radix_prefix_sum_256.comp` are deleted.
- Scratch sizing becomes capacity-dependent (one caller-provided buffer holding the transposed histogram plus the scan's block sums); `RadixSort` owns its internal `ParallelScan` and rebuilds it when the capacity grows. No buffer is allocated inside the algorithm.
- Solver: the three entry passes write a key array and a payload array; the sentinel `kInvalidKey` becomes `body_count` (so the key bound is tight and honest); `SumByKey`'s level 0 reads the payload array instead of a `uvec2` pair array.
- Broad phase: pair generation writes packed `a * shape_count + b` keys; `flag_unique.comp`/`compact_scatter.comp` operate on `uint` keys; a new `unpack_pairs.comp` restores the canonical `uvec2` pairs and publishes `pair_count`, replacing the detector's `DispatchCopy`; `shape_count <= 65536` is asserted at `Configure` (the value at which `shape_count * (shape_count - 1)` already wraps in three existing places).
- Lagrange reset restored: `clear_hinge_lagrange.comp` and `clear_fixed_lagrange.comp` are loaded and dispatched every substep again. They still exist and are still listed in the shader inventory, but no code dispatches them since the atomic-float removal, so `hinge_anchor`, `fixed_rotation` and `fixed_position` lagrange buffers are never cleared (the hinge axis is cleared by an integer-clear workaround).
- Tests: the radix test is rewritten for the new contract (including a stability assertion the old suite never had), `CompactUnique` gets its first test, and the headless stability probe is registered in CTest.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `gpu-radix-sort`: the record layout, the stability guarantee, the pass-count derivation, the sizing helpers, the result-buffer contract, the algorithm's sub-steps and the shader inventory all change; three stale requirements describing the removed `RenderGraphBuilder`/`AddPasses` API are removed.
- `gpu-compact-unique`: the sorted input and compacted output are `uint` keys rather than `uvec2` pairs.
- `gpu-sum-by-key`: level 0 reads a key array plus a payload-index array instead of a `uvec2` pair array.
- `spatial-hash-broad-phase`: the dedup stage's record format (packed keys), the new unpack-and-publish step, and the shape-count bound.
- `xpbd-contact-solve`: the per-substep lagrange reset is generalised from the contact buffer to every constraint type's multiplier buffers.

## Impact

Code: `engine/Physics/gpu_algorithm/{RadixSort,SumByKey,CompactUnique}.{h,cpp}`, `engine/Physics/shader/algorithm/{radix_scatter,radix_block_histogram,sum_by_key,flag_unique,compact_scatter}.comp`, `engine/Physics/shader/collision/SpatialHashBroadDetector/{generate_broad_pairs,generate_global_pairs,unpack_pairs}.comp`, `engine/Physics/Collision/SpatialHashBroadDetector.cpp`, `engine/Physics/Solver/XPBDGpuSolver.cpp`, `engine/Physics/shader/solver/XPBDSolver/common/xpbd_entry_layout.glsl` and `entries/*.comp`.

Tests: `test/engine/headless/gpu_radix_multiblock_test.cpp` (rewritten), `test/engine/headless/gpu_radix_primary_test.cpp` (removed or folded in), `test/engine/headless/gpu_compact_unique_test.cpp` (new), `test/engine/headless/gpu_sum_by_key_test.cpp`, `test/app/physics/CMakeLists.txt` (register the existing `physics_app_stability_test`).

Process: `openspec/changes/remove-float-atomics` and `xpbd-entry-count-driven-reduce` are unarchived and already carry `gpu-radix-sort`/`gpu-sum-by-key` deltas that this change supersedes; they must be archived (or their conflicting deltas dropped) before this change is archived.

Deliberately **not** fixed here, to be recorded elsewhere: the unbounded `atomicAdd(pair_count)` slot write in the three pair generators (their `max_output_pair_count` bound is a post-dedup count, not an emission count); the append-only shape-slot high-water mark and the buffer sizes derived from it; `max_global_shape_count` acting as a Y dispatch dimension that silently drops pairs for shapes beyond it; `ComputeStage`/`ComputeResourceBinding` lifetime, synchronisation, reallocation and leak issues; and `CompactUnique` still loading `memset_uint.comp` from the `collision/` directory.
