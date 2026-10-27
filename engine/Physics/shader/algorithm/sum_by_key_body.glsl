// sum_by_key_body.glsl — Shared body of the SumByKey segmented reduction.
//
// Included by sum_by_key.comp (GPU-produced entry count, read from a bound
// buffer) and sum_by_key_push.comp (CPU-known entry count, taken from the push
// block).
//
// Reduces a key-ascending array of records (uint key + N float channels) into
// per-key sums: `OutValues[c * max_key_value + key]` is the sum of channel c
// over the records carrying `key`.  One workgroup handles one block of 256
// records, and the same body runs at every recursion level: a block whose run
// continues into a neighbouring block passes that partial run on as a boundary
// record.
//
// Input, selected by `gather_payload`.  Both shapes are channel-major, with the
// value channel stride `input_count`:
//   - gather: the caller's sorted key array plus its payload-index array —
//     `key = KeysIn[e]`, channel c at `ValuesIn[c * input_count + PayloadIn[e]]`;
//   - record: the record arrays — `key = KeysIn[e]`,
//     channel c at `ValuesIn[c * input_count + e]`.
//
// Level geometry.  The *level element count* `input_count` is the length of the
// array the level reads: the caller's capacity at level 0, and `R_i = 2 *
// ceil(R_{i-1} / 256)` at level i — the record count the previous level wrote,
// one pair of slots per block.  It alone sizes the per-block real extent, the
// record-region partition, the channel stride and the dispatch.  Level 0
// dispatches `ceil(input_count / 256)` workgroups, so it rewrites every record
// slot of the next region on every call; a final level is a single workgroup.
//
// Bound.  A level reads only the elements below `bound = min(entry_count,
// input_count)`: an element at or beyond it has no key, no payload index and no
// value to read, and carries the out-of-range pad key with a contribution of
// 0.0.  The bound never changes the geometry above.
//
// Merge.  A run is a maximal span of equal keys.  The block merges runs in
// shared memory by segmented pairwise doubling: in the round for spacing `d`
// (1, 2, 4, ... 128) an invocation adds the value at `tid - d` to its own when
// that element exists and carries the same key.  Every invocation joins every
// executed round, and the rounds stop once no run's last element still needs to
// merge — an exit read after a barrier, so the block leaves together.
//
// Settlement.  Each run's last element writes the run's in-block sum: straight
// to `OutValues` when the run lies inside the block, otherwise as a record at
// slot `2*wg` (the run reaches the block's first element) and/or `2*wg+1` (it
// reaches the block's last real element) for the next level to merge.  A block
// that is a single run writes an all-zero second record, and a key at or above
// `max_key_value` is never written out.
//
// Every record slot of a region is rewritten on every call: a block whose first
// element is at or beyond the bound leaves before any barrier, and if the level
// writes records it writes its two record slots as pad records first.
//
// Dispatch: ceil(input_count / 256), 1, 1.

layout(set = 0, binding = 0) readonly  buffer KeysIn     { uint  v[]; } keys_in;
layout(set = 0, binding = 1) readonly  buffer ValuesIn   { float v[]; } values_in;
// The caller's payload-index array, read in gather mode.
layout(set = 0, binding = 2) readonly  buffer PayloadIn    { uint  v[]; } payload_in;
layout(set = 0, binding = 3) buffer    RecKeys          { uint  v[]; } rec_keys;
layout(set = 0, binding = 4) buffer    RecValues        { float v[]; } rec_values;
layout(set = 0, binding = 5) buffer    OutValues        { float v[]; } out_values;

// Out-of-range key pad: larger than every key a caller can produce and at or
// above every possible `max_key_value`, so it forms one trailing run that is
// never written out.
const uint kOobKey   = 0xFFFFFFFFu;
// Shared-memory channel capacity; `num_channels` is at most this.
const uint kMaxChannels = 8u;

shared uint s_key[256];
// Channel-major, like the value and output buffers: the merge walks one channel
// at a time with a stride-1 access across invocations.
shared float s_val[8][256];
// One exit flag per merge round.  A round's slot is never reused, so no round
// needs to reset it.
shared uint s_pending[8];

// Runs one level of the reduction for one 256-element block of the dispatch.
//
// Every parameter below is a push constant of the including shader's block,
// except `entry_count`, which the value-count form takes from the push block and
// the buffer-count form reads from its `EntryCount` binding.
//
//   - `input_count`   — level element count `R_i`: the length of the array this
//     level reads (the caller's capacity at level 0, the previous level's record
//     count above).  At least 1.
//   - `num_channels`  — float channels per record, in `[1, kMaxChannels]`.
//   - `max_key_value` — number of output slots: a key below it is writable, a
//     key at or above it is never written out.  At least 1.
//   - `record_stride` — next record region's per-channel stride
//     (`2 * ceil(input_count / 256)`); zero exactly when this level writes no
//     records, which is the final level.
//   - `gather_payload` — 1 at level 0, where the keys and payload indices come
//     from the caller's arrays; 0 at every deeper level, where they come from
//     the record arrays.
//   - `entry_count`   — how many leading elements carry real data; the level's
//     bound is `min(entry_count, input_count)`.
void SumByKeyRun(
    uint input_count,
    uint num_channels,
    uint max_key_value,
    uint record_stride,
    uint gather_payload,
    uint entry_count
) {
    const uint wg  = gl_WorkGroupID.x;
    const uint tid = gl_LocalInvocationID.x;

    // The level's data length; see the header.
    const uint bound = min(entry_count, input_count);

    // Block's span of the level's records.  real_count = number of records in
    // this block (may be < 256 for the last block).
    const uint base       = wg * 256u;
    const uint real_count = min(256u, input_count - min(base, input_count));
    const uint e          = base + tid;

    // A block whose first element is at or beyond the bound holds no live
    // element.  The predicate is uniform across the workgroup, so the block
    // leaves together and before every barrier.
    if (base >= bound) {
        // The level still rewrites this block's record slots: a later level
        // reads its region over the full level element count.
        if (record_stride != 0u) {
            const uint slot_base = 2u * wg;
            rec_keys.v[slot_base] = kOobKey;
            rec_keys.v[slot_base + 1u] = kOobKey;
            for (uint c = 0u; c < num_channels; ++c) {
                rec_values.v[c * record_stride + slot_base] = 0.0f;
                rec_values.v[c * record_stride + slot_base + 1u] = 0.0f;
            }
        }
        return;
    }

    // Cooperative load.  `real` gates the value load; `elem_key` is the pad key
    // for an element the bound excludes.
    const bool live = (e < bound);
    bool real = live;
    uint elem_key = kOobKey;
    uint value_index = e;

    if (live) {
        elem_key = keys_in.v[e];
        if (gather_payload != 0u) {
            // Gather mode (level 0 only): the payload array names the slot this
            // element's values are read from.  A payload index the bound
            // excludes contributes zero, and the key stays as read, because it
            // is real data and the run structure depends on the keys ascending.
            value_index = payload_in.v[e];
            real = (value_index < bound);
        }
    }

    s_key[tid] = elem_key;
    for (uint c = 0u; c < num_channels; ++c) {
        s_val[c][tid] = real ? values_in.v[c * input_count + value_index] : 0.0f;
    }
    // One flag per round, published inside the round it belongs to.
    if (tid < 8u) s_pending[tid] = 0u;
    barrier();

    // The run's last element is the only writer, but every invocation joins the
    // merge rounds below, so none may leave before the last round.
    const bool is_run_last =
        (tid == 255u) || (s_key[tid] != s_key[tid + 1u]);

    // ---- Segmented pairwise doubling: ceil(log2(run length)) rounds ----
    //
    // A run-last invocation keeps the block in the loop only while it may still
    // be incomplete: its window was unclamped (reaches past index 0) and its
    // merge succeeded.  The flag is read after a barrier, so the block leaves
    // together.
    for (uint r = 0u, d = 1u; d < 256u; d <<= 1u, ++r) {
        // Clamped so the predicated load below cannot form an out-of-range
        // shared address.
        const uint partner = (tid >= d) ? (tid - d) : tid;
        const bool merge = (tid >= d) && (s_key[tid] == s_key[partner]);

        // Read the partner first: the add writes the slot every invocation reads.
        float partner_val[8];
        for (uint c = 0u; c < kMaxChannels; ++c) {
            partner_val[c] =
                (merge && c < num_channels) ? s_val[c][partner] : 0.0f;
        }
        if (merge && is_run_last && (tid >= (d << 1))) {
            s_pending[r] = 1u;
        }
        barrier();

        if (merge) {
            for (uint c = 0u; c < num_channels; ++c) {
                s_val[c][tid] += partner_val[c];
            }
        }
        barrier();

        if (s_pending[r] == 0u) {
            break;
        }
    }

    if (!is_run_last) {
        return;
    }

    const uint key = s_key[tid];

    // The run's last element is this invocation.  Ascending keys make "the run
    // reaches the block's first element" exactly `key == s_key[0]`.
    const bool touches_first = (key == s_key[0]);
    const bool touches_last  = (tid + 1u == real_count);
    const bool is_final_level = ((input_count + 255u) / 256u) == 1u;
    const bool valid_key = (key < max_key_value);

    if (is_final_level) {
        // One workgroup spans the level: every run is complete.
        if (valid_key) {
            for (uint c = 0u; c < num_channels; ++c) {
                out_values.v[c * max_key_value + key] = s_val[c][tid];
            }
        }
        return;
    }

    const uint slot_base = 2u * wg;

    if (!touches_first && !touches_last) {
        // The run lies inside the block: finalise it.
        if (valid_key) {
            for (uint c = 0u; c < num_channels; ++c) {
                out_values.v[c * max_key_value + key] = s_val[c][tid];
            }
        }
        return;
    }

    // The run may continue next door: emit an in-block partial as a record.
    if (touches_first) {
        rec_keys.v[slot_base] = key;
        for (uint c = 0u; c < num_channels; ++c) {
            rec_values.v[c * record_stride + slot_base] = s_val[c][tid];
        }
    }
    if (touches_last) {
        if (touches_first) {
            // One run fills the block: a zero record keeps the chain dense
            // without double counting.
            rec_keys.v[slot_base + 1u] = key;
            for (uint c = 0u; c < num_channels; ++c) {
                rec_values.v[c * record_stride + slot_base + 1u] = 0.0f;
            }
        } else {
            rec_keys.v[slot_base + 1u] = key;
            for (uint c = 0u; c < num_channels; ++c) {
                rec_values.v[c * record_stride + slot_base + 1u] = s_val[c][tid];
            }
        }
    }
}
