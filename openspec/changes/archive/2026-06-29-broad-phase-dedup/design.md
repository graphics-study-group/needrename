## Context

The broad-phase collision detector (`SpatialHashBroadDetector`) runs a GPU pipeline: AABB computation → two-pass cell assignment → counting sort by cell ID → within-cell upper-triangle pair generation → global-shape pair generation. Two problems exist:

1. **Cross-cell duplicates**: When shapes A and B share N grid cells, `generate_broad_pairs` produces the pair (A,B) N times (once per cell). The narrow phase then runs MPR N times, and the solver applies duplicate constraints causing over-correction.

2. **Out-of-bounds shapes marked global**: Shapes entirely outside the grid are marked "global" and paired with all alive shapes, producing N−1 wasteful pairs per out-of-bounds shape.

The project already has `ParallelScan` — a reusable GPU prefix-sum module in `engine/Physics/gpu_algorithm/` that follows a specific design pattern (self-contained class, owns only shaders + small per-pass param buffers, all large working buffers caller-provided). The new `RadixSort` and `CompactUnique` modules follow this same pattern.

### Constraints
- All computation is GPU-side via Vulkan compute shaders (GLSL 450)
- Render graph construction happens at record time (no GPU execution during build)
- Per-pass parameter buffers must be independent (sharing a host-visible buffer across passes would cause the last CPU write to overwrite earlier pass parameters)
- Max shape count is 2^20 (1,048,576)

## Goals / Non-Goals

**Goals:**
- Eliminate cross-cell duplicate collision pairs via GPU sort + unique
- Stop marking out-of-bounds shapes as global; silently ignore them
- Create reusable `RadixSort` and `CompactUnique` GPU algorithm modules
- Validate shape count ≤ 2^20, throw on exceed

**Non-Goals:**
- Porting these algorithms to CUDA or non-Vulkan backends
- Optimizing the radix sort to skip zero upper-bits (future work)
- Changing the narrow phase or solver to deduplicate (should be handled at broad phase)
- Changing the cell size or grid configuration

## Decisions

### Decision 1: 8-bit LSD Radix Sort over 4-bit or Hash-based alternatives

**Chosen**: 8-bit LSD radix sort (8 passes total: 4 for secondary key `b`, 4 for primary key `a`).

**Rationale**: 8-bit radix requires only 8 passes (vs 16 for 4-bit), each with a 256-entry histogram. The histogram fits comfortably in a single workgroup for the prefix-sum step. Hash-based dedup with `atomicCompSwap` on GPU was considered but rejected due to divergent control flow (linear probing), unpredictable memory access, and hash-table sizing complexity.

**Alternatives considered**:
- *4-bit radix*: 16 passes, 16-entry histogram. Too many passes for marginal memory savings.
- *Hash-set dedup*: Single-pass in theory but GPU-divergent probing kills performance. Rejected.
- *Structural dedup* (only emit pair in canonical cell): Requires knowing which other cells contain a given pair — complex and invasive. Rejected.

### Decision 2: Sort by (b) then (a) for correct (a,b) ordering

**Chosen**: LSD radix sort processes secondary key `b` first (passes 0-3), then primary key `a` (passes 4-7).

**Rationale**: LSD radix sort is stable. Sorting by `b` first groups all pairs by `b`, then sorting by `a` moves them into `a`-primary order while preserving the `b` order within equal `a` groups. Result: sorted by `(a, b)` as desired.

Since both `a` and `b` are ≤ 2^20, the top 12 bits of each word are always zero. Passes 2 and 6 (byte 2) process mostly zeros. Passes 3 and 7 (byte 3) are entirely no-op copies. We keep all 8 passes for correctness and simplicity; the no-op pass overhead is minimal for typical pair counts in the thousands.

### Decision 3: Dedicated 256-element prefix sum shader (not ParallelScan)

**Chosen**: A dedicated `radix_prefix_sum_256.comp` with `local_size_x = 256`, single workgroup, shared-memory Blelloch scan. No external dependency.

**Rationale**: `ParallelScan` is designed for arbitrary-size arrays with multi-level recursion. For a fixed 256-element scan, a single shared-memory pass is simpler, faster (no global memory round-trips), and avoids the external dependency. The algorithm is identical to `parallel_scan.comp`'s core but hard-coded for 256.

### Decision 4: Reuse histogram as atomic counter during scatter

**Chosen**: After exclusive prefix sum on the 256-entry histogram, use the same buffer directly for `atomicAdd` during scatter.

**Rationale**: The existing `scatter_sort.comp` already uses this pattern (prefix-summed offsets used as atomic counters). After scatter, the histogram is corrupted (contains end positions), but the next radix pass clears it first. No separate copy-to-scratch step needed — saves one dispatch per radix pass.

### Decision 5: CompactUnique as a separate module

**Chosen**: `CompactUnique` is its own class in `engine/Physics/gpu_algorithm/`, separate from `RadixSort`.

**Rationale**: Compact-unique is a general operation (not coupled to sorting). It can be used independently. Following the ParallelScan pattern, it's a self-contained module with its own shaders and param pool. The broad detector combines them: RadixSort → CompactUnique.

### Decision 6: Module API follows ParallelScan pattern exactly

**Chosen**: Both `RadixSort` and `CompactUnique` expose:
- Constructor `(RenderSystem&, uint32_t max_elem_count)`
- Static sizing helpers
- `AddPasses(...)` that takes all working buffers as parameters (handles + ComputeBuffer refs)
- Internal param pool with per-dispatch buffer allocation
- Lazy shader initialization on first `AddPasses`

### Decision 7: Out-of-bounds shapes ignored entirely

**Chosen**: In `compute_aabbs`, shapes entirely outside `[world_min, world_max]` write a degenerate (zero-size) AABB at `world_min`, set `global_flags = 0`, and do NOT append to `global_list`. In `count_cells` and `fill_cells`, an explicit out-of-bounds check returns early.

**Rationale**: An out-of-bounds shape shouldn't interact with any in-bounds shape. Marking it "global" generates useless pairs. A zero-size AABB at `world_min` ensures the cell-range computation doesn't crash; the explicit out-of-bounds check in count/fill ensures no cells are generated.

### Decision 8: Shape count validation is a hard error

**Chosen**: `RadixSort` throws `std::runtime_error` if `max_shape_count > 2^20`.

**Rationale**: The user chose exception over warning. Silent sorting bugs from overflowed packed keys are far worse than a crash with a clear error message.

## Risks / Trade-offs

- **[Risk] Radix sort adds 24 dispatches per frame (8 passes × 3 steps) + CompactUnique adds 3 dispatches** → Mitigation: Each dispatch is lightweight (primarily memory-bound scatter/gather). For typical pair counts (thousands), total overhead is ~0.1-0.2ms. The savings from eliminating duplicate narrow-phase work far outweigh this cost.

- **[Risk] 8 full passes even though top 12 bits are always zero** → Mitigation: Correctness first. Passes with all-zero digits are effectively memory copies. Future optimization could skip passes 3 and 7 unconditionally, or scan the actual max shape index to determine how many bytes to process.

### Decision 9: Dispatch for buffer capacity, read actual count from GPU buffer

**Chosen**: All dedup passes (RadixSort histogram/scatter, CompactUnique flag/copy/scatter) dispatch workgroups for `max_pairs` (buffer capacity), not the actual pair count. The actual pair count is bound as a GPU buffer (`pair_count_buf`) and read by each shader at execution time.

**Rationale**: At RenderGraph build time, the GPU has not executed the pair-generation passes yet — `pair_count` is stale/garbage if read via `GetVMAddress()`. Dispatching for the maximum and skipping out-of-range threads via `if (idx >= pair_count.count) return;` in the shader is the only correct approach.

**Anti-pattern explicitly rejected**: Reading host-visible GPU buffers at RG build time to determine dispatch counts. This pattern would produce incorrect results because the RG is lazily built once (or on infrequent rebuilds), long before GPU execution.

#### Scenario: Zero pairs at execution time

- **WHEN** the pair-generation passes produce zero pairs (no collisions this frame)
- **THEN** all dedup shader threads check `idx >= pair_count.count` (0) and return immediately
- **AND** no out-of-bounds access occurs
- **AND** the subsequent `unique_count → pair_count` copy writes 0

## Risks / Trade-offs

- **[Risk] Dispatching for max_pairs adds idle GPU threads** → Mitigation: The idle threads only execute a single bounds check and return. The overhead is negligible (~a few microseconds of GPU time) compared to the correctness guarantee and the savings from eliminating duplicate narrow-phase work.

- **[Risk] Out-of-bounds shapes are invisible to all collision** → Mitigation: This is intentional. Objects outside the simulation world have no valid physics interactions. If an object is teleported back inside, its next-frame AABB will be in-bounds and it resumes colliding.

- **[Trade-off] Dedup is a post-processing step rather than structural** → Mitigation: Structural dedup (canonical-cell approach) would require knowing shared-cell topology during pair generation, which is complex and invasive. Post-processing sort+unique is simpler, correct, and the overhead is small.
