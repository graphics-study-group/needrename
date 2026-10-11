## Why

The XPBD contact solver intermittently applies no position correction at all: every collision's accumulate pass produces the right per-contact correction, but the per-body reduction publishes zero for most bodies, so `apply_body_position_deltas` sees an entry count with almost no contacts behind it. The reduction's level 0 reads the caller's arrays over their **whole capacity**, and `radix_copy_back.comp` writes that whole capacity back from the sort's own scratch, so the sentinel tail the caller wrote past its entry count is replaced by stale keys left by the largest sort the instance has ever run. A stale key that names a live body forms a second equal-key run, and the segmented reduction's last-element write for that run overwrites the body's real sum with zero. Because the corruption depends on the largest entry count seen so far rather than on the current frame, the failure is intermittent, grows with the number of bodies, and becomes rarer when a debugger or RenderDoc changes the frame's timing and history.

The same mismatch is why the tail of the caller's array cannot be relied on at all: nothing between the caller's full-capacity entry pass and the reduction's level 0 ever guarantees what occupies the slots at or beyond the entry count.

## What Changes

- **One bound decides what a reduction level may read.** `sum_by_key_body.glsl` computes `bound = min(entry_count, input_count)` and applies it to keys and payload indices alike: an element at or beyond the bound reads neither the key array nor the payload-index array, carries the out-of-range pad key and contributes `0.0`. The level element count still alone defines the geometry (workgroup count, per-block real extent, record partition, channel strides), so the record chain stays dense and an entry count equal to the level element count remains the identity behaviour. This is what makes the caller's tail unreadable rather than merely valueless.
- **`radix_copy_back.comp` copies only the sorted prefix.** It binds the call's element count and returns for an index at or beyond it, so a sort never writes the caller's arrays past the count and the entry-pass sentinel tail survives a sort unchanged. This also makes the copy-back's effect independent of the pass count's parity in the only way that matters to a caller.
- **A level's workgroup at or beyond the bound leaves immediately.** The whole workgroup returns before any barrier; a level that writes records still writes its two record slots first, so the next level's region is rewritten in full on every call and no stale record can be read.
- **The `gpu_algorithm` shader headers state the current contract only.** The headers of the radix and `SumByKey` shaders are cut down to what the files do and the invariants they keep: no rationale for a rejected alternative, no history of what the code used to be, no worked examples or arithmetic derivations. `SumByKeyRun`'s parameters are each documented with their meaning, source and range.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `gpu-sum-by-key`: the two-length contract is replaced by a single bound; an element at or beyond `min(entry count, level element count)` reads neither keys nor payload indices and carries the pad key; an out-of-range payload index below the bound keeps its key; a block entirely beyond the bound still writes its record slots; the reduction's header comment and `SumByKeyRun`'s parameter documentation are specified.
- `gpu-radix-sort`: `radix_copy_back.comp` binds the element count and copies only `[0, count)`, leaving the caller's arrays at or beyond it untouched; the parity-independence requirement is restated in terms of that bound.
- `physics-gpu-shaders`: a new requirement governs the header comments of the shaders under `engine/Physics/shader/algorithm/` and the parameter documentation of their entry functions.

## Impact

- `engine/Physics/shader/algorithm/sum_by_key_body.glsl` — the bound, the workgroup early-out, the header comment, and `SumByKeyRun`'s parameter documentation.
- `engine/Physics/shader/algorithm/sum_by_key.comp`, `sum_by_key_push.comp` — header comments and the push-block comment lines.
- `engine/Physics/shader/algorithm/radix_copy_back.comp` — the count binding, the counted copy, and the header comment.
- `engine/Physics/shader/algorithm/radix_block_histogram.comp`, `radix_scatter.comp` — header comments only.
- `engine/Physics/gpu_algorithm/RadixSort.cpp` / `RadixSort.h` — the copy-back dispatch binds the count buffer; the class documentation states that only `[0, count)` is sorted or written.
- `engine/Physics/gpu_algorithm/SumByKey.h` — the class documentation's two-length paragraph is restated as the single bound, and `Record`'s parameter documentation follows.
- No public API change: `Record` signatures, push-block layouts and buffer sizes are unchanged, and no caller has to pass anything new. Existing callers that write a full-capacity tail (the entry passes) become correct rather than reading their own sentinel back.
