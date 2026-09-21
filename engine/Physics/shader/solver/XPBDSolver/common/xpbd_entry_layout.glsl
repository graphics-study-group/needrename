// xpbd_entry_layout.glsl — Channel and slot layout shared by the XPBD entry /
// accumulate / SumByKey pipeline.
//
// The solver reduces per-body Jacobi partials with SumByKey.  Each constraint
// type (contact / hinge / fixed) maintains its own sorted entry list and its own
// channel-major value buffer.  This file fixes the shared conventions so every
// stage indexes them identically.
//
// Value channels (per record, in one packed channel-major buffer):
//   0..2 : Δlin xyz
//   3..5 : Δang xyz
//   6    : contribution flag (0.0 = skipped, 1.0 = contributed this iteration)
//
// A per-type sorted entry is a **key array plus a payload array** (a
// struct-of-arrays record).  `key` encodes slot *ownership* only: the index of
// the body the slot belongs to, or the invalid-slot key when the slot has no
// owner this substep (no such contact/joint, or an out-of-range owner index).
// Entry passes read no body state; whether a body actually receives a
// contribution is decided by the accumulate shaders' guards, and a slot that is
// never written stays inert because the value buffer is cleared every iteration.
//
// The invalid-slot key is `body_count` (the live body slot count), which the
// entry passes read from `RigidBodyAlive.length()`.  It is deliberately *not* a
// fixed constant: it is larger than every real key, so unowned slots sort after
// every owned one, and it is exactly the `max_key_value` the solver passes to
// SumByKey, which drops every key at or above that bound.
//
// The payload is the entry's own **slot id**, and it is used twice: the radix
// sort permutes it together with its key, and SumByKey's level-0 gather reads
// this entry's values from exactly `slot` in the channel-major value buffer.  So
// an accumulate pass writes at the entry's own slot index and no permutation map
// exists anywhere in the path.  Because the sort permutes the payload array in
// place, every entry pass must rewrite it on every dispatch — see the CONTRACT
// note at the top of each entries/*.comp.
//
// Slot ids:
//   contact : e = cidx * 2 + side        (side in {0 = body A, 1 = body B})
//   hinge   : e = j * 4 + side * 2 + c   (2 bodies x {axis, anchor} constraints)
//   fixed   : e = j * 4 + side * 2 + c   (2 bodies x {rotation, position})

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
// live body slot count, read per dispatch as `RigidBodyAlive.length()`.  There
// is deliberately no constant for it here — see the note above.

// Hinge/fixed: 4 slots per joint = 2 bodies x 2 scalar constraints.
const uint kJointSlotBase = 4u;   // slots per joint
const uint kJointAxisSlot = 0u;   // within a body's pair: axis / rotation constraint
const uint kJointAnchorSlot = 1u; // within a body's pair: anchor / position constraint

// Contact: 2 slots per contact point = 2 bodies x 1 scalar constraint.
const uint kContactSlotBase = 2u;

// Slot ids — shared by the entry passes (which write them) and the accumulate
// shaders (which use them as their scatter index).
uint contact_slot(uint cidx, uint side) { return cidx * kContactSlotBase + side; }

uint joint_slot(uint j, uint side, uint constraint) {
    return j * kJointSlotBase + side * 2u + constraint;
}

// Buffer layout convention (shared by the entry passes, the accumulate scatter
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
//   - apply merges those per-body outputs with the same stride

#endif // XPBD_ENTRY_LAYOUT_GLSL
