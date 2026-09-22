## Why

`SumByKey`'s block-internal reduction sums each key run with two serial loops executed by a single
invocation: a leftward shared-memory key scan to find the run start, then an element-by-element add
loop. Its *work* is optimal (each entry is added exactly once), but its *depth* is the run length —
up to 255 dependent shared-memory steps plus `256 * num_channels` adds on one lane, roughly an order
of magnitude slower than the rest of the block, while the other invocations have already returned.
A run of a few hundred entries is routine in the solver (a ground body's contact slots form one run),
so every substep's critical path can end in that tail.

This is the follow-on work `xpbd-entry-count-driven-reduce` recorded but did not do: *"the reduction's
block-internal sum is a run-local linear scan; replacing it with a segmented log-depth reduction is
its own change with its own measurement."* It is also where the implementation and the spec already
disagree: `gpu-sum-by-key` requires "same-key pairwise doubling with dead marking", which the shipped
shader never did.

## What Changes

- Replace the run-local linear scan and enumeration with a **segmented pairwise merge over the block's
  shared values**: `ceil(log2(longest run in the block))` rounds of `if (key[tid] == key[tid - d])
  val[tid] += val[tid - d]`, `d` doubling each round. Depth becomes `O(log run length)` and every
  invocation works in every round.
- **Delete the leftward run-start scan.** With ascending keys, "the run starts at the block's first
  element" is exactly `key[tid] == key[0]`, so the classification needs one shared read instead of
  `O(run length)` dependent steps.
- **Adaptive round count.** A per-round shared flag records whether any run-last invocation still
  needed to merge; the block exits uniformly once none does, so blocks of short runs pay 1-3 rounds
  instead of a fixed 8.
- **Channel-major shared value array** (`s_val[num_channels][256]` instead of `s_val[256][8]`): the
  strided access the merge needs is then bank-conflict-free for every channel count, and it matches
  the channel-major layout the value and output buffers already use.
- **Correct the spec to match.** The segment guard is one key comparison in an ascending-key array; the
  requirement's "dead marking" and the design's "aliveness suffix scan" describe a heavier formulation
  that is not needed and was never implemented. The requirement is rewritten around the actual
  algorithm and the ascending-key precondition it depends on.
- Add an **isolated kernel micro-benchmark** (headless test) across a run-length matrix, because the
  previous change established that scene-level wall clock cannot resolve this class of saving and the
  engine has no GPU timestamp facility.
- Unchanged on purpose: the record contract (at most two boundary records per block, region geometry
  and offsets), `kBlockSize`, the level chain `R_{i+1} = 2 * ceil(R_i / 256)`, the capacity/entry-count
  semantics, the shader's bindings and push-constant block, and the entire C++ API.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `gpu-sum-by-key`: the block-internal reduction requirement changes from a linear run-local scan and
  enumeration to a segmented log-depth merge whose segment guard is defined by the ascending-key
  structure, and the requirement stops asserting the "dead marking" machinery the implementation never
  had.

## Impact

- `engine/Physics/shader/algorithm/sum_by_key.comp` — the whole in-block reduction body and the shared
  array declarations. This is the only shader affected.
- `engine/Physics/gpu_algorithm/SumByKey.h` / `.cpp` — **not** touched: the level geometry, the record
  regions, the push constants and the bindings are all unchanged, so the change is confined to one
  file on the C++ side of the boundary (none).
- `test/engine/headless/gpu_sum_by_key_test.cpp` — the existing scenarios (including the single key
  spanning 10000 entries) must stay green; new cases cover a run-length matrix (1, 2, 3, 7, 8, 9, 255,
  256, 257) and the highest channel count.
- `test/engine/headless/` — a new isolated benchmark target that records the reduction many times over
  synthetic pair/value buffers and times the submission, with no scene, collision detection or solver
  in the measurement.
- **Numerical note:** the summation order changes from left-to-right to a balanced merge tree, so sums
  may differ from today in the last bits. The spec already declares within-key order unspecified and
  allows rounding to vary with order; results stay deterministic for a fixed input.
- **No new device requirement.** The merge uses no subgroup operations and no extension, preserving the
  "any Vulkan 1.3 device" property that `remove-float-atomics` established.
- **Ordering dependency:** the `gpu-sum-by-key` requirements this delta modifies currently exist only
  inside the unarchived `remove-float-atomics` (in progress) and `xpbd-entry-count-driven-reduce`
  (complete) changes. Those must be archived, in that order, before this delta can apply.
- No public API breaks; every affected buffer, shader and algorithm entry point is solver-internal or a
  `gpu_algorithm` class used only by the solver.
