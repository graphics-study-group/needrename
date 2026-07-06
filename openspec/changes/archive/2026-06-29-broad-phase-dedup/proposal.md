## Why

The broad-phase collision detector currently produces duplicate candidate pairs when two shapes share multiple grid cells. These duplicates cause the narrow phase to run redundant MPR detection and the XPBD solver to apply duplicate constraints, leading to over-correction and wasted GPU time. Additionally, shapes that fly entirely outside the grid bounds are incorrectly marked "global" and paired with every alive shape — generating N−1 unnecessary pairs each. Both issues inflate `pair_count` and degrade physics quality.

## What Changes

- **Out-of-bounds shapes are silently ignored** instead of being marked global. In `compute_aabbs`, shapes entirely outside the grid no longer set `global_flags` or append to `global_list`. In `count_cells` and `fill_cells`, an explicit out-of-bounds check skips such shapes. The "global" concept now means only "spans too many cells."

- **New GPU algorithm module: `RadixSort`** — a reusable 8-bit LSD radix sort for `uvec2` pairs, following the same pattern as `ParallelScan` (self-contained class in `engine/Physics/gpu_algorithm/`, owns only shaders and per-pass parameter buffers, all large buffers caller-provided). Sorts by `(a, b)` where `a` is the primary key.

- **New GPU algorithm module: `CompactUnique`** — a reusable dedup-compact pass that takes a sorted `uvec2` array, marks unique entries, performs prefix sum, and scatters unique entries to a compact output. Follows the same self-contained pattern.

- **Broad-phase dedup integration** — after within-cell and global pair generation, the detector sorts collision pairs via `RadixSort` (8 passes, ping-pong on the pairs buffer), then compacts duplicates via `CompactUnique`. Output overwrites the original `collision_pairs[]` buffer; `pair_count` is updated.

- **Max shape count validation** — `RadixSort` validates that `max_shape_count ≤ 2^20` (1,048,576) and throws `std::runtime_error` if exceeded.

## Capabilities

### New Capabilities
- `gpu-radix-sort`: Reusable GPU 8-bit LSD radix sort algorithm for `uvec2` pairs, following the `ParallelScan` self-contained module pattern. Owns shaders and per-pass parameter buffers; all working buffers caller-provided via `AddPasses`.
- `gpu-compact-unique`: Reusable GPU compact-unique post-processing pass. Takes a sorted `uvec2` array, outputs deduplicated contiguous pairs. Owns shaders and per-pass parameter buffers.

### Modified Capabilities
- `spatial-hash-broad-phase`: **BREAKING** — The "Global shape handling" requirement changes: shapes entirely outside grid bounds are no longer marked global. They are silently ignored (no pairs generated). Only shapes spanning > `max_cells_per_shape` cells are marked global. After pair generation, a dedup step (RadixSort + CompactUnique) runs before returning output buffers.
- `global-pair-generation-pass`: The "Shape outside bounds marked global" scenario is removed. The global list now only contains shapes that span too many cells (never out-of-bounds shapes).

## Impact

- **New files**: `engine/Physics/gpu_algorithm/RadixSort.h`, `engine/Physics/gpu_algorithm/RadixSort.cpp`, `engine/Physics/gpu_algorithm/CompactUnique.h`, `engine/Physics/gpu_algorithm/CompactUnique.cpp`
- **New shaders**: `engine/Physics/shader/algorithm/radix_histogram.comp`, `engine/Physics/shader/algorithm/radix_prefix_sum_256.comp`, `engine/Physics/shader/algorithm/radix_scatter.comp`, `engine/Physics/shader/algorithm/flag_unique.comp`, `engine/Physics/shader/algorithm/compact_scatter.comp`
- **Modified shaders**: `compute_aabbs.comp`, `count_cells.comp`, `fill_cells.comp`
- **Modified C++**: `SpatialHashBroadDetector.cpp` (out-of-bounds logic, dedup integration, new buffer allocations)
- **Dependencies**: `RadixSort` and `CompactUnique` depend only on `RenderSystem`, `ComputeBuffer`, `ComputeStage`, `ComputeResourceBinding`, `RenderGraphBuilder` — same layering as `ParallelScan`
