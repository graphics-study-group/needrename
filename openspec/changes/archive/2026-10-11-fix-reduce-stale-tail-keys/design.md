## Context

See `proposal.md` — Why for the observed failure. The state that shapes the approach:

- `SumByKey`'s level 0 reads the caller's key array and payload-index array over the call's **capacity** (`ceil(capacity / 256)` workgroups, `R_0 = capacity`), because the level element count alone sizes the geometry and the record chain. The entry count bounds only which *values* are read.
- The caller is an entry pass that writes the whole capacity: real entries below its entry count, and a sentinel above it (key = `body_count`, payload = the slot's own index).
- `RadixSort` sorts `[0, count)` into its own ping-pong partner arrays and, when the derived pass count is odd, runs `radix_copy_back.comp`, which copies **the whole capacity** back into the caller's arrays. The partner array's tail has never been written by any pass, so the copy-back publishes whatever the largest sort on that instance last left there.
- The reduction is segmented: a run's last element writes that run's sum, and the only write gate is `key < max_key_value`. A stale tail key equal to a live key forms a second run for that key, whose last element writes `0.0` over the real sum. Per-channel store interleaving explains a half-overwritten vector (`flag` correct, `lin` zeroed).

Two properties of the geometry are why this looked intermittent:

- The capacity is derived from the broad-phase pair count (`all_pairs * 5`, bounded by `max_contact_points` — 24150 slots for 70 bodies), while only a few hundred slots carry entries, so the tail is the overwhelming majority of every level.
- The pass count is `ceil(bit_width(max_key_value) / 8)`: 66 bodies → one pass → a copy-back is recorded. A key bound above 256 gives two passes, no copy-back, and no visible corruption; and the corruption depends on the largest entry count the buffers have ever held, so a debugger or a capture that changes that history hides it.

## Goals / Non-Goals

**Goals:**

- Make an entry count a real bound on what a level reads — keys, payload indices and values alike — at every level and in both input modes.
- Make a sort unable to write the caller's arrays above the elements it sorted.
- Keep every geometry the callers depend on unchanged: workgroup counts, record-region partition, channel strides, push-block layouts, `Record` signatures, buffer sizes.
- Leave the algorithm shaders' headers stating the current contract, with every parameter of the reduction entry function documented.

**Non-Goals:**

- Not a change to how the entry passes emit entries, to the capacity derivation, or to the callers' sentinel values.
- Not a change to the meaning or the order of the existing descriptor bindings.
- Not an audit of shader comments outside `engine/Physics/shader/algorithm/`.
- Not new validation, tests or diagnostics beyond what the caller's own test applications already do.

## Decisions

### One bound, applied to keys and payload indices alike

A level computes `bound = min(entry_count, input_count)` and applies it element by element: at or beyond it, neither the key array nor the payload-index array is read, the element carries the pad key and contributes `0.0`; below it, the existing rules are unchanged (a payload index at or beyond the bound contributes `0.0` and keeps its key, so a caller that emits a bad index loses one entry instead of a run).

This is the only formulation that is independent of the caller's tail *content*. Alternatives considered:

- **Have the entry pass write a zeroed tail.** Insufficient: the sort's copy-back rewrites the tail, so its content is not the caller's to control — that is precisely the second half of this bug.
- **Keep today's contract and bound the tail's *values* only.** That is the bug: the key still participates in run classification and can form a second run.
- **Keep the tail's keys but suppress writes from elements at or beyond the bound.** Needs a per-element flag carried through the level chain, where the pad key already provides one for free.

### The pad key is the out-of-range maximum

`0xFFFFFFFF` compares at or after every key a caller can produce, so replacing the tail's keys leaves the array ascending — the property the merge and the boundary classification rely on. It is also beyond every possible `max_key_value` (a `uint32_t`), so the existing `key < max_key_value` gate keeps a pad run out of the output for any caller, without a new condition anywhere in the write paths.

### The bound applies at every level, from the same entry count

The live record extent of level `i+1` is `2 * ceil(bound_i / 256)` records, and the bound at that level is `min(entry_count, R_{i+1})`, which is never smaller than that for `bound_i >= 2`. Records above the live extent are pad records the previous level wrote, and those can never write out. Alternative considered: use the level element count as the bound for levels `>= 1` (the old "a deeper level's input is entirely valid" claim). It happens to work, but it leaves a second rule to keep true while depending on the same full-region rewrite; one rule with one proof is the smaller obligation.

### A workgroup at or beyond the bound leaves early, and record levels still write their slot pair

The tail can be the majority of a level's dispatch, so a dead block should not run the shared-memory load and the merge rounds at all. The guard `base >= bound` is uniform across the workgroup and sits before every barrier, so the return is well defined; a *partially* live block keeps the existing predication, since its body still contains barriers.

At a level that emits records, a dead block writes its two record slots (pad key, zero value) before returning. The next level reads its region over the full `R_i`, so a slot left unwritten would be read as a record of an earlier call. The pair is an ordinary all-pad record pair: a run touching both ends of a block, carrying no value, merging into the same pad run at the next level.

### The copy-back is bounded by the element count

The count is the number of elements the sort ordered, and it is the same buffer the histogram and the scatter already read. The copy-back's source array is shared working storage whose tail has never been written, so a capacity-wide copy cannot be made correct by construction — it must simply not write there. Alternative considered: dispatch `ceil(count / 64)` workgroups. Rejected: the dispatch geometry would then follow a GPU-side value, which this module deliberately never does, since the count is unknown at record time; a capacity-sized dispatch with a counted body preserves that rule.

### Shader headers state the current contract

The headers keep what a reader of the file cannot get elsewhere: the contract, the invariants, the dispatch geometry, the buffer layout. They drop the rationale for design decisions, the narration of what the code replaced, and worked examples. That text has to be maintained, duplicates `openspec/specs/` and the C++ class documentation, and it is what went stale here — the header of `sum_by_key_body.glsl` documented the two-length contract that this change removes. The reduction's entry function documents each parameter where it is declared.

## Risks / Trade-offs

- **[A caller depended on the tail's keys taking part in run classification]** → No caller's results did: the tails in question are the entry passes' sentinels, which are at or above `max_key_value` and therefore never wrote. For every element below the bound the behaviour is unchanged, so the change makes the guarantee stronger rather than different.
- **[Levels above 0 now read slots the previous level wrote as pad records]** → They already read their whole region on every call, and both the normal path and the early-out path write every slot of that region, so a region is fully rewritten by each call.
- **[The extra binding could desynchronize a descriptor layout]** → It is appended after the copy-back's four existing bindings and resolved by name through the same kernel-resource path as the other algorithm shaders; a missing or misnamed binding fails binding validation immediately rather than silently.
- **[The pass parity that exposes the defect]** → A key bound above one byte makes a sort record two passes, no copy-back, and the symptom disappears without the defect being fixed. The regression scenario pins the one-pass case (a full-capacity tail survives an odd pass count), and the defect itself — the copy-back's write extent — is fixed rather than hidden.
- **[An entry count above the capacity]** → Now clamped by the bound; previously payload indices up to the entry count could be read out of the value buffer's capacity. Strictly safer, and no caller passes such a count.

## Migration

None. No public API, push-block layout or buffer size changes; callers recompile only. The affected call site is the contact group's reduction — the only one whose input is also radix-sorted — while the joint groups' reductions (CPU-known counts, unsorted) get the same bound with no behavioural difference.
