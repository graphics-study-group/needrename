// xpbd_scatter.glsl — Per-slot scatter of Jacobi partials (replaces the
// pre-change float atomicAdd accumulation).
//
// This file MUST be included *after* the including shader has declared its one
// entry-list binding, because the helper writes it directly:
//
//   layout(...) buffer ScratchValues { float v[]; } scratch_values;
//
// The scratch is channel-major with stride == the entry capacity and is indexed
// by the entry's own **slot id** (see common/xpbd_entry_layout.glsl).  The entry
// pass stores that slot id as the payload of the sorted `(key, slot)` pair, so
// SumByKey's level-0 gather reads the value this helper wrote at exactly the
// same index.  No permutation map exists anywhere in the path, and for contacts
// the write is even coalesced (`slot = contact_index * 2 + side`, so adjacent
// invocations write adjacent addresses).

#ifndef XPBD_SCATTER_GLSL
#define XPBD_SCATTER_GLSL

#include "xpbd_entry_layout.glsl"

// Write one contribution into the scratch at the slot's own index.
// `capacity` is the entry capacity (== the scratch's channel stride).
void scatter_slot(uint slot, uint capacity, vec3 lin_delta, vec3 ang_delta) {
    if (slot >= capacity) return;

    scratch_values.v[kChanLinX * capacity + slot] = lin_delta.x;
    scratch_values.v[kChanLinY * capacity + slot] = lin_delta.y;
    scratch_values.v[kChanLinZ * capacity + slot] = lin_delta.z;
    scratch_values.v[kChanAngX * capacity + slot] = ang_delta.x;
    scratch_values.v[kChanAngY * capacity + slot] = ang_delta.y;
    scratch_values.v[kChanAngZ * capacity + slot] = ang_delta.z;
    scratch_values.v[kChanFlag * capacity + slot] = 1.0f;
}

#endif // XPBD_SCATTER_GLSL
