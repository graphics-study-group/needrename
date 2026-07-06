## Context

The `SpatialHashBroadDetector` needs parallel prefix-sum to compute exclusive scans over two arrays: per-shape cell counts (→ write offsets into `cell_shape_pairs`) and per-cell shape histograms (→ read offsets for scatter-sort). The current inline implementation at line 731 works only for `N ≤ 512` (single workgroup). A multi-level scan orchestration (`DispatchParallelScan`, lines 290–388) was commented out due to two defects:

1. **Parameter overwrite**: `gpu_scan_params` is a single host-visible buffer written via `GetVMAddress()` before each `AddPass`. Since `AddPass` only queues work, all subsequent writes overwrite prior parameters before any dispatch executes. All passes read the last-written value.
2. **Duplicate render-graph handles**: `ImportExternalResource` was called multiple times for the same `block_sums_buf`, creating multiple `RGBufferHandle` values for one physical buffer. The render graph relies on handle identity to track dependencies, so barriers between scan levels were invisible to it.

The render graph executes all passes sequentially on one command buffer (see `RenderGraphPass.h:90-94`), so ordering is deterministic. The issue is purely about data visibility: the per-pass parameters must be written at pass-execution time, not at pass-registration time.

## Goals / Non-Goals

**Goals:**
- Create a reusable `ParallelScan` class under `engine/Physics/gpu_algorithm/` with no dependency on the broad-phase detector
- Single-call API: one `AddPasses` invocation handles single- or multi-level internally
- Per-pass parameter buffers: each dispatch binds its own 12-byte host-visible buffer (`mode`, `block_offset`, `elem_count`), written once at registration and never overwritten
- Separate input/output buffers always (the shader reads `InputData` and writes `OutputData`; caller provides distinct buffers)
- Multi-level scan support for arbitrary element counts (recursive block-sum scanning)
- Proper `UseBuffer` declarations on all accessed buffer handles, enabling correct barrier insertion
- Locate shader at `engine/Physics/shader/algorithm/parallel_scan.comp`

**Non-Goals:**
- Modifying the scan algorithm itself (Blelloch work-efficient scan remains)
- Supporting in-place scan (always separate input/output — the caller may pass the same buffer for both if truly desired)
- GPU push-constant support (requires pipeline-layout changes beyond this change's scope)
- Optimizing for sub-512 element counts beyond what single-workgroup dispatch already provides

## Decisions

### Decision 1: Per-pass parameter buffer per dispatch

**Chosen**: Each `AddPass` call creates (or reuses from a pool) a dedicated 16-byte host-visible `ComputeBuffer` holding `{mode, block_offset, elem_count, _pad}`. The buffer is written once during setup and bound to that specific pass's resource binding. The shader uses `block_offset` as an index offset into `BlockSums` for both mode=1 writes and mode=2 reads, enabling future non-overlapping sub-block-sum placement.

**Alternatives considered**:
- *Write inside pass function lambda*: `GetVMAddress()` write + dispatch inside the `SetPassFunction` callback. Correct for serial execution but fragile if the render graph ever parallelizes pass recording. Also hides the dependency from the graph.
- *Push constants*: Ideal for small per-dispatch data but not wired through `ComputeStage`. Adding push-constant support to `ComputeStage` is a separate, larger change.

**Rationale**: Per-pass buffers are explicit, race-free, and work within the existing binding model. Each buffer is 12 bytes; for a 3-level scan this is 36 bytes total. The pool avoids repeated allocation overhead.

### Decision 2: Separate input/output buffer bindings always

**Chosen**: The new shader always treats `InputData` and `OutputData` as separate bindings. The caller passes two `ComputeBuffer&` references (which may alias if the caller wants in-place behavior, but that's the caller's choice, not the class's responsibility).

**Rationale**: The working single-level scan in BroadDetector already does this (`shape_cell_count` → `cell_offsets`). The old `parallel_scan.comp` also has separate bindings. No shader-level change to this pattern is needed. The `ParallelScan` class just exposes what the shader already supports.

### Decision 3: `max_elem_count` at construction time

**Chosen**: The constructor takes `uint32_t max_elem_count` and pre-sizes the internal block-sums scratch buffer to `ceil(max_elem_count / 512) * sizeof(uint32_t)`.

**Rationale**: The user requested this. It avoids dynamic reallocation inside `AddPasses`, which could invalidate `RGBufferHandle` values mid-frame. The BroadDetector knows its maximum element count at construction (max of `shape_count` and `grid_total_cells + 1`).

### Decision 4: Single-call API (`AddPasses`)

**Chosen**: `ParallelScan::AddPasses(builder, input_handle, output_handle, input_buf, output_buf, elem_count)` internally determines single-level vs. multi-level and adds 1 or 3 passes to the builder.

**Rationale**: The multi-level orchestration is an implementation detail. Callers should not care whether the scan is single- or multi-level. The class encapsulates all complexity.

### Decision 5: Class location and ownership

**Chosen**: `engine/Physics/gpu_algorithm/ParallelScan.h/.cpp` — a standalone utility class. It owns:
- `ComputeStage` (the scan pipeline)
- A pool of small parameter buffers (`std::vector<std::unique_ptr<ComputeBuffer>>`)

It does NOT own:
- The input/output data buffers (passed by reference)
- The block-sums scratch buffer (caller-provided; sized via `GetRequiredBlockSumsBytes`)
- The render-graph handles (passed in, including `block_sums_handle`)

The scratch buffer and its handle are caller-managed.  The caller imports the buffer
once into the render graph and passes the same handle to every `AddPasses` call,
guaranteeing correct dependency tracking without handle aliasing.

**Rationale**: The user requested `gpu_algorithm` to avoid tight coupling with the detector. The class is a pure GPU algorithm utility usable by any physics subsystem.

### Decision 6: Shader binding layout

| Binding | Name | Access | Source |
|---------|------|--------|--------|
| 0 | `InputData` | readonly buffer uint[] | Caller-provided |
| 1 | `OutputData` | buffer uint[] | Caller-provided |
| 2 | `ScanParams` | readonly buffer (3 uints: mode, block_offset, elem_count) | Per-pass param buffer |
| 3 | `BlockSums` | buffer uint[] | Class-owned scratch or dummy |

The `ElemCount` binding from the old shader is folded into `ScanParams` as the third uint, reducing binding count from 5 to 4.

### Decision 7: Recursive multi-level with overlapping buffer regions

**Chosen**: When `num_blocks > 512` (requiring >2 scan levels), `AddScanInternal` calls itself recursively with `block_sums_buf` serving as both data and block-sum storage. Sub-block totals are written to `block_sums[0..sub_blocks-1]` (using `block_offset=0`), temporarily overwriting the first entries. The recursive scan's mode-2 add-back restores correct values before the root level's mode-2 pass reads them.

**Alternatives considered**:
- *Separate sub-block-sums buffer*: Allocate a second scratch buffer for the recursive scan's block sums. Correct but wastes memory and complicates buffer management.
- *Non-overlapping offset (block_offset ≠ 0)*: Write sub-block sums to `block_sums[num_blocks..]` using `block_offset=num_blocks`. Requires a data-offset parameter in the shader so sub-level data reads are also offset. Complex and unnecessary — the overlap approach is correct because the recursive scan's mode-1 and mode-2 passes operate on the same data region, and the root scan's mode-3 only reads the scanned block sums after the recursive scan completes.

**Rationale**: The Blelloch scan within each workgroup preserves all information in shared memory. The block-total write at `block_sums[wg_id]` only overwrites a scanned output value that the recursive scan's mode-2 will restore. For the root caller, the recursive scan is an opaque "scan this buffer" operation — whether sub-levels temporarily corrupt and restore sub-regions is an implementation detail. The approach requires exactly `max_workgroups` scratch entries, matching the root-level requirement.

**Block-sums buffer sizing**: `block_sums_entries` is computed as the geometric series `M + ceil(M/512) + ceil(ceil(M/512)/512) + ...` where `M = ceil(max_elem_count / 512)`. The series terminates when `level_blocks ≤ 1` (single-workgroup scans use mode=0 and write no block sums). For practical `max_elem_count ≤ 2^20`, this requires at most `M + 5` entries (3 terms).

## Risks / Trade-offs

- **Memory overhead**: Each scan pass allocates a 12-byte parameter buffer. For the BroadDetector's two scan sites (shape offsets + cell offsets), worst case is 6 tiny buffers ≈ 72 bytes. Negligible.
- **Shader migration**: The existing `parallel_scan.comp` under `XPBDSolver/` must be removed or redirected if consumed elsewhere. → Grep for references before deleting; leave a forwarding comment if needed.
- **Block-sums sizing**: If `max_elem_count` is underestimated, the scratch buffer is too small. → Add a runtime check in `AddPasses` that asserts `elem_count <= m_max_elem_count`.
- **Recursive multi-level**: For arrays requiring >2 scan levels (`max_elem_count > 262k`), `AddScanInternal` recursively calls itself with the block-sums buffer acting as both data and block-sum storage. Sub-block totals temporarily overwrite `block_sums[0..sub_blocks-1]` and are restored by the recursive mode-2 add-back (see Decision 7). The block-sums buffer is geometrically sized to hold entries for all recursion levels.

## Open Questions

None — all design decisions were resolved in discussion.
