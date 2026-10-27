## Context

See `proposal.md` — Why for the motivation; `specs/gpu-sum-by-key/spec.md` for the requirement being changed.

Constraints that shape the approach:

- **Frozen surface.** `kBlockSize` (256), the level chain `R_{i+1} = 2 * ceil(R_i / 256)`, the record-region
  partition, the push-constant block, the seven bindings, the C++ entry points and the "at most two boundary
  records per block" contract are all pinned by specs and tests. The change must fit inside the block body.
- **The block body owns 9 KB of shared memory**: `s_key[256]` (1 KB) plus the value array (8 KB at
  `num_channels = 8`). Occupancy at 256 threads/block is already at the 8-blocks-per-SM point on a 100 KB SM.
- **`num_channels` is a runtime push constant in `[1, 8]`**, so anything per-channel is a loop, not a
  compile-time unroll.
- **Keys are ascending within the level's element range** (`.x` sorted by the radix sort at level 0; the record
  regions the upper levels read are emitted in block order). The unowned tail carries `0xFFFFF` and the
  out-of-capacity padding carries `0xFFFFFFFF`, both at the end of the range, so the order holds at every level.
- **The requirements this change modifies are not in `openspec/specs/` yet.** They live in the unarchived
  `remove-float-atomics` (in progress) and `xpbd-entry-count-driven-reduce` (complete) changes, in that order.
- **Measurement is per-change scope here.** The `xpbd-entry-count-driven-reduce` verification note records that
  its saving was real but unresolvable in scene-level wall clock, and the engine has no GPU timestamp facility
  (`engine/` has no `vkCmdWriteTimestamp` wrapper).

## Goals / Non-Goals

**Goals:**

- Bound the block-internal reduction's depth at `log2(256) = 8` rounds regardless of run length, with every
  invocation working in every executed round.
- Not tax blocks whose runs are short: the round count follows the block's longest run.
- Remove the leftward run-start scan and the per-element enumeration entirely.
- Keep the record contract, the level geometry, the bindings, the push constants and the C++ API unchanged.
- Close the implementation/spec divergence: the spec's "dead marking" describes machinery that was never
  implemented and is not needed.
- Produce a measurement that isolates the reduction kernel from the solver.

**Non-Goals:**

- Changing `kBlockSize`, the level chain, or the number of boundary records per block. A larger block becomes
  cheaper once depth is logarithmic, but it is a separate change (pinned spec numbers, shared-memory budget).
- The radix sort's capacity-derived dispatch (the other follow-on in the same note) — indirect dispatch.
- Removing level 0's gather-by-slot indirection, or restructuring the settle/write phase.
- Subgroup operations. `subgroupShuffleUp` needs `shaderSubgroupShuffleRelative`, which Vulkan does not
  guarantee, and the engine has no subgroup usage; `remove-float-atomics` deliberately removed an optional
  device-feature dependency.
- Bit-identical reproduction of the current sums (the merge order changes; see Risks).

## Decisions

### Decision 1: Segmented pairwise doubling as the block-internal merge

**Choice**: after the load barrier, run `ceil(log2(longest run in block))` rounds (`d = 1, 2, 4, ... 128`), where
invocation `t` adds the value at `t - d` to its own value when `t >= d` and the two keys are equal. The value
array is written in place each round. After the last round each invocation's value is the sum of the elements of
its own run that lie in the block, up to and including itself, so a run's last invocation holds its run's in-block
sum.

**Why the guard is sufficient, and why no dead marking is needed.** The invariant after round `d` is
`val[t] = sum(run(t) ∩ [t - 2d + 1, t])` (window clamped at 0; an empty intersection contributes 0). A merge at
`t` is legal exactly when `[t - 2d + 1, t]` contains no run boundary beyond what `val[t]` already omits. The guard
compares the two window endpoints; because keys are ascending, equal endpoint keys imply that every key in
between is equal, hence the window lies inside one run and both operands are complete sums of their halves. When
the guard fails, `val[t]` keeps the value it had, which by the invariant is the run-suffix of the smaller window —
and that is already the final answer, because the run start is at or after the smaller window's start. This is why
the previous design's DEAD marking and its 8-round aliveness suffix scan are unnecessary: they belong to a
Blelloch *pairing* formulation, not to this access pattern.

**Consequences for classification**: `run_start == 0` (the "touches the block's first element" test) is
equivalent to `key[tid] == key[0]` under ascending keys, so the run-start scan disappears with no replacement.
`touches_last` stays `tid + 1 == real_count`.

**Rejected**:

- *Segmented Blelloch upsweep + downsweep* (the literal `parallel_scan.comp` shape): the disjoint pairing needs
  only one barrier per round and does half the adds, but it needs `8 + 1 + 8 = 17` fixed barriers plus a second
  set of guards (`key[idx-d] == key[idx]` up, and a carry reset when `key[idx-d] != key[idx-d+1]` down), and it
  cannot stop early: a run longer than the current window does not guarantee a merge at that round (a run of
  length 5 with `d = 2` merges at no valid node), so there is no sound early exit. Short-run blocks would pay all
  17 barriers where the serial code pays almost nothing.
- *Keep the enumeration, replace only the run-start scan with an exponential + binary search*: `O(log L)` for the
  scan but still `O(L)` dependent adds in one invocation, so the depth stays linear. Kept only as a baseline.
- *Subgroup shuffle scan*: not portable (see Non-Goals).

### Decision 2: Adaptive round count with a per-round shared flag

**Choice**: a `shared uint s_pending[8]`, one slot per round, cleared before the load barrier. In round `r` an
invocation writes `1` into `s_pending[r]` if and only if it is the last element of its run, its merge succeeded,
and `tid >= 2 * d`; after the barrier every invocation reads `s_pending[r]` and the loop exits uniformly when it
is `0`.

**Why that predicate is exact**: after round `d` an invocation is still incomplete exactly when its window was
neither clamped to 0 nor stopped by a key mismatch. A key mismatch means the window already covers its run start
(complete); a clamped window means the window reaches index 0 (complete, since a run cannot start below 0). A
successful merge in an unclamped window (`tid >= 2d`) is therefore the only incomplete case. The resulting round
count is `ceil(log2(longest run in the block))`.

**Why it is safe**: completeness is monotone — a later successful merge would require an equal key below the
invocation's run start, which ascending order forbids — so a flag that is not set stays unset and the exit is not
premature. The flag is written before the barrier that follows the merge and read after it, so no read/write race
exists, and because each round has its own slot there is no reset to order. The exit condition is read from shared
memory, so every invocation of the block takes the same branch (a divergent `barrier()` would be undefined
behaviour). Only run-last invocations write flags, so invocations that merely merge in passing cannot prolong the
block.

**Rejected**:

- *Fixed 8 rounds*: simplest, and a reasonable first step, but it makes a block of one-entry runs pay 16 barriers
  and 8 rounds of shared traffic where the current code pays one comparison.
- *Per-invocation span tracking plus an atomic counter*: shared atomics on one address serialize (256 ops), which
  costs more than the rounds they would save.
- *A host-supplied round hint in the push-constant block*: the host cannot know a block's longest run, and the
  push block is frozen.

### Decision 3: Channel-major (SoA) shared value array

**Choice**: `shared float s_val[8][256]` with the channel count as the first index, matching the channel-major
layout the value and output buffers already use.

**Why**: the merge's accesses are `s_val[c][tid - d]` and `s_val[c][tid]`, which are stride-1 across invocations
and therefore bank-conflict-free for every `num_channels`. The current AoS declaration `s_val[256][8]` has a
stride of 8 between consecutive elements, so a warp touching 32 different run elements hits only 4 banks (8-way
conflict). It happens to be conflict-free at `num_channels = 7` because only the declared row stride counts, not
the used one — the solver's 7 channels do not save it. The load path is unchanged in cost: the same gather is
issued either way (the global read is scattered by payload slot; the shared write is then stride-1 per channel).

**Rejected**: keeping AoS (8-way conflicts at the maximum channel count); padding the row to 9 (`s_val[256][9]`
is conflict-free at the cost of 1 KB of shared memory, and it is not general).

### Decision 4: One shared value buffer, two barriers per round

**Choice**: read the partner into registers, barrier, add into the value array, barrier, then test the exit flag.
Shared memory stays at 9 KB.

**Why**: `val[t] += val[t-d]` is read- and write-same-address; without the mid-round barrier an invocation could
read a partner value that had already absorbed the window, double counting it. The alternative — two value buffers
with the parity alternating per round — needs one barrier per round but doubles the value array (7-8 KB more),
taking occupancy from 8 to about 6 blocks per SM, and level 0 is a gather whose latency benefit depends on
having more blocks in flight. The two extra barriers per round are cheaper than that.

**Rejected**: double-buffered values (occupancy); relying on the Blelloch disjoint pairing to avoid the mid-round
barrier (that is a different algorithm — Decision 1).

### Decision 5: Measurement isolates the kernel

**Choice**: a new headless benchmark target that allocates synthetic pair/value/record/output buffers, records
`Record` `K` times inside one command buffer with the required barriers between them, submits once and times the
submission. Inputs cover the run-length matrix (all-distinct keys; runs of 2, 8, 40; one key spanning the whole
capacity; a ground-body-like mixture) and the solver's channel count. The numbers are reported, not asserted.

**Why**: the previous change proved that a scene-level wall clock cannot resolve a few tens of microseconds
against a ~21 ms step dominated by fixed costs, and the engine has no timestamp facility.

**Rejected**: scene-level timing (proven unresolvable); adding `vkCmdWriteTimestamp2` support to the Rhi layer as
part of this change (a new capability with its own API and validation surface — worth its own change if the
benchmark turns out to be too noisy).

### Decision 6: The spec is corrected rather than the implementation conformed to

**Choice**: rewrite the requirement's block-internal paragraph around the actual merge and its ascending-key
guard, and drop the "dead marking" claim.

**Why**: the shipped shader never implemented dead marking, and the guard analysis above shows the marking is not
needed for this access pattern. Conforming the implementation to the old wording would add state for no benefit.
The `xpbd-entry-count-driven-reduce` proposal already recorded the linear scan as follow-on work, so the two
changes are consistent: this one finishes that item and fixes the wording.

## Risks / Trade-offs

- **[Divergent barrier → undefined behaviour]** → the exit test is read from shared memory after a barrier, so
  the loop is block-uniform; the existing `if (!is_run_last) return;` moves after the loop; the round bound
  `d < 256` is a compile-time constant. Tests include non-power-of-two run lengths to catch a guard that only
  happens to work on aligned runs.
- **[Short-run regression]** → adaptive exit keeps the round count at 1-2 for blocks whose runs are 1-2 elements;
  the benchmark reports the L=1 and L=2 cases explicitly instead of only the worst case.
- **[Summation order changes]** → last-bit differences from today's left-to-right sums. The spec already declares
  within-key order unspecified and permits rounding to vary; results remain deterministic for a fixed input. Test
  tolerances already accommodate this.
- **[The record chain's density and the entry-count tail are the parts most likely to break silently]** → every
  existing scenario in `gpu_sum_by_key_test.cpp`, including the capacity/entry-count and gather cases, must stay
  green; no case in that file is modified except by addition.
- **[Benchmark noise on shared CI]** → the benchmark reports numbers and is not a pass/fail gate; if it cannot
  resolve the difference either, that is recorded in the verification notes rather than papered over.
- **[Ordering with unarchived changes]** → this delta modifies a requirement that currently exists only inside
  `remove-float-atomics` and `xpbd-entry-count-driven-reduce`; those must be archived, in that order, before this
  delta applies.

## Migration Plan

Two commits, each independently buildable:

1. `sum_by_key.comp` rewritten (merge, adaptive exit, SoA values) plus the new run-length test cases. Rollback is
   a revert of one file.
2. The isolated benchmark target plus its CMake entry.

Archive order: `remove-float-atomics`, then `xpbd-entry-count-driven-reduce`, then this change.

## Implementation notes (recorded after implementation)

Points where the plan did not survive contact with the code, and the measurement.

- **Tasks 1.2 and 1.3 cannot be separated.** The doubling needs every invocation alive in every round,
  so the merge and the removal of `if (!is_run_last) return;` have to land in the same working
  revision. The run-start scan survived that step (still feeding `touches_first`) and was removed in
  1.4 as planned.
- **Task 1.5's mutation check names the wrong mutation.** Dropping the `tid >= 2 * d` term (or the
  `is_run_last` guard) makes the exit flag *more* eager, never premature, so it cannot produce a wrong
  sum: the run-length matrix still passed with it removed (measured, not assumed). The mutation that
  does fail the matrix is removing the flag write altogether — with it, `gpu_sum_by_key_test` fails
  immediately (`single key spanning many blocks expected 10000 got 2`). The implementation is
  unchanged; the task's verification clause is superseded by this.
- **Removing the mid-round barrier did not fail the suite on this device.** The read-then-write race it
  prevents (`val[tid] += val[tid - d]` reads the slot another invocation writes in the same round) did
  not manifest on the RTX 3090 under Debug. The barrier is kept because the single-buffer design's
  correctness depends on it; no deterministic test can assert a missing barrier.
- **The per-run register copy is gone.** The settle writes read `s_val[c][tid]` directly instead of the
  old `float sum[8]`; `touches_last` uses `tid` rather than the removed `run_end` alias.
- **Shared memory grew by 32 bytes**: `s_key` 1 KB + `s_val[8][256]` 8 KB + `s_pending[8]` 32 B =
  9248 B (was 9216 B). SPIR-V audit: seven storage buffers and a 20-byte push-constant block, both
  unchanged; no subgroup or group-non-uniform opcode and no `#extension` directive in the source.
- **A pre-existing descriptor-offset violation surfaced while running the benchmark.** With validation
  layers active, `num_channels = 7` reports
  `vkUpdateDescriptorSets(): offset (50104) must be a multiple of minStorageBufferOffsetAlignment (16)`.
  The record-region value offsets are `region_count * (1 + num_channels) * 4` bytes apart and are not
  16-byte aligned. This change does not cause it — the region partition is untouched — and the test
  suite never surfaced it because it runs without validation layers on the loader path. It is
  deliberately **not** fixed here: the fix changes `GetRequiredRecordsBytes` and the pinned
  `R_{i+1} = 2 * ceil(R_i / 256)` geometry that this change freezes. It should be its own change.

### Measurement

`gpu_sum_by_key_bench` on an NVIDIA GeForce RTX 3090, Debug build, Vulkan SDK 1.4.313.2, 7 channels,
50 recorded reductions per submission, one submit and `vkDeviceWaitIdle`. Three shaders went through
the same binary: the pre-change `HEAD` shader (row-major shared values + serial run scan), a variant
with only the channel-major layout applied to it, and the merge version. Each cell lists the wall
clock per reduction in ms for each repeat run:

| case | capacity | pre-change | layout only | merge |
| --- | --- | --- | --- | --- |
| one key spans all | 1000 | 0.0588 / 0.0707 / 0.0592 | 0.0678 / 0.0548 | 0.0140 / 0.0140 / 0.0142 |
| one key spans all | 20000 | 0.0506 / 0.0516 / 0.0506 | 0.0503 / 0.0606 | 0.0160 / 0.0154 / 0.0154 |
| one key spans all | 200000 | 0.3425 | — | 0.3303 |
| all distinct keys | 20000 | 0.0563 / 0.0676 | 0.0535 / 0.0431 | 0.0492 / 0.0346 / 0.0591 |
| runs of 2 | 20000 | 0.0417 / 0.0420 | 0.0382 / 0.0389 | 0.0258 / 0.0282 / 0.0268 |
| runs of 8 | 20000 | 0.0432 / 0.0418 | 0.0513 / 0.0383 | 0.0310 / 0.0353 / 0.0304 |
| mixture | 20000 | 0.0569 / 0.0450 | 0.0418 / 0.0420 | 0.0149 / 0.0179 / 0.0151 |

Findings:

- **The case the change targets is 3.3x faster, above noise**: one key spanning the array at capacity
  20000 goes from 0.0509 ms +- 0.0006 (three runs) to 0.0156 ms +- 0.0003 (three runs).
- **The layout change is not measurably responsible.** The layout-only variant tracks the pre-change
  shader on every row, so the gain is the algorithm, not the bank conflicts Decision 3 was chosen for.
- **At capacity 200000 the difference disappears** (every row within run-to-run noise). With 782
  level-0 blocks there are enough blocks in flight to hide block latency, so the dispatch is
  throughput-bound on level-0's gather and the scattered output writes, and the block merge stops
  mattering. The solver is in the opposite regime: `xpbd-entry-count-driven-reduce` measured a contact
  capacity of 18300 slots (72 blocks), which leaves block latency exposed — the regime where this pays.
- Noise on this machine is about +-2% on the long-run row and up to +-10% on the rows with many
  scattered output writes (20000 keys).
- The harness times submit-to-`waitIdle`, so every figure includes per-submission overhead with a floor
  of roughly 13 us per reduction at capacity 1000, where the element work is negligible. The per-case
  ratios at equal capacity are the meaningful output, not the absolute floor.

## Open Questions

- Whether the benchmark should be registered with `ctest` (skipped by default, or run as a plain target) is a
  test-infrastructure detail that can be settled during implementation without changing the specs, the approach
  or the task breakdown.
- Whether `kBlockSize` should grow now that depth is logarithmic is deferred to its own change (it would change
  the pinned level geometry).
