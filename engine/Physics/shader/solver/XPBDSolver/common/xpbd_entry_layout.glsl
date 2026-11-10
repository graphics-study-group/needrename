// xpbd_entry_layout.glsl — Channel and slot layout shared by the contact entry /
// accumulate / SumByKey pipeline.
//
// The solver reduces per-body Jacobi partials with SumByKey.  Only the contact
// group goes through that pipeline: joints are solved by a direct pose write in
// a color pass (see common/xpbd_joint_solve.glsl), so they have no entry list
// and no value buffer.  This file fixes the contact group's conventions so every
// stage indexes them identically.
//
// Value channels (per record, in one packed channel-major buffer):
//   0..2 : Δlin xyz
//   3..5 : Δang xyz
//   6    : contribution flag (0.0 = skipped, 1.0 = contributed this iteration)
//
// A sorted entry is a **key array plus a payload array** (a struct-of-arrays
// record).  `key` encodes slot *ownership* only: the index of the body the slot
// belongs to, or the invalid-slot key when the slot has no owner this substep
// (no such contact, or an out-of-range owner index).  The entry pass reads no
// body state; whether a body actually receives a contribution is decided by the
// accumulate shader's guards, and a slot that is never written stays inert
// because the value buffer is cleared every iteration.
//
// The invalid-slot key is `body_count` (the live body slot count), which the
// entry pass receives as a per-dispatch value in its push-constant block.  It is
// deliberately *not* a fixed constant: it is larger than every real key, so
// unowned slots sort after every owned one, and it is exactly the
// `max_key_value` the solver passes to SumByKey, which drops every key at or
// above that bound.  It is also deliberately not read from a buffer's length:
// under the capacity contract a length is an allocation size, not the live slot
// count.
//
// The payload is the entry's own **slot id**, and it is used twice: the radix
// sort permutes it together with its key, and SumByKey's level-0 gather reads
// this entry's values from exactly `slot` in the channel-major value buffer.  So
// an accumulate pass writes at the entry's own slot index and no permutation map
// exists anywhere in the path.  Because the sort permutes the payload array in
// place, the entry pass must rewrite it on every dispatch — see the CONTRACT
// note at the top of entries/contact_entries.comp.
//
// Slot ids:
//   contact : e = cidx * 2 + side        (side in {0 = body A, 1 = body B})

#ifndef XPBD_ENTRY_LAYOUT_GLSL
#define XPBD_ENTRY_LAYOUT_GLSL

const uint kChanLinX = 0u;
const uint kChanLinY = 1u;
const uint kChanLinZ = 2u;
const uint kChanAngX = 3u;
const uint kChanAngY = 4u;
const uint kChanAngZ = 5u;
const uint kChanFlag = 6u;
const uint kNumValueChannels = 7u;

// Key used for slots with no owner, and for out-of-range owner indices: the
// live body slot count, received per dispatch in the push-constant block.  There
// is deliberately no constant for it here — see the note above.

// Contact: 2 slots per contact point = 2 bodies x 1 scalar constraint.
const uint kContactSlotBase = 2u;

// Slot ids — shared by the entry pass (which writes them) and the accumulate
// shader (which uses them as its scatter index).
uint contact_slot(uint cidx, uint side) { return cidx * kContactSlotBase + side; }

// Buffer layout convention (shared by the entry pass, the accumulate scatter
// and SumByKey):
//   - the sorted entry list is a **key array plus a payload array**; the radix
//     sort orders it by key and permutes the payload along with it
//   - the entry value buffer is channel-major with stride == entry_capacity and
//     is indexed by **slot**:
//       value(channel, slot) = values[channel * entry_capacity + slot]
//   - SumByKey's level-0 input is that same key array plus that same payload
//     array (`KeysIn` / `PayloadIn`), with `max_key_value == body_count`; it
//     reads an entry's value at
//     `values[channel * entry_capacity + payload]`
//   - SumByKey's per-body output is channel-major with stride == body_count:
//       out(channel, body) = out[channel * body_count + body]
//   - the contact position apply averages those per-body outputs by the contact
//     contribution count

#endif // XPBD_ENTRY_LAYOUT_GLSL
