// xpbd_scatter.glsl — Per-slot scatter of Jacobi partials (replaces the
// pre-change float atomicAdd accumulation).
//
// This file MUST be included *after* the including shader has declared its two
// entry-list bindings, because the helper writes them directly:
//
//   layout(...) readonly buffer PosOf         { uint  v[]; } pos_of;
//   layout(...) buffer          ScratchValues { float v[]; } scratch_values;
//
// `pos_of[slot]` is the sorted position the entry pass reserved for that slot
// (built once per substep by invert_permutation); the scratch is channel-major
// with stride == entry capacity (see common/xpbd_entry_layout.glsl).

#ifndef XPBD_SCATTER_GLSL
#define XPBD_SCATTER_GLSL

#include "xpbd_entry_layout.glsl"

// Write one contribution into the scratch at the slot's sorted position.
// `capacity` is the entry capacity (== the scratch's channel stride).
void scatter_slot(uint slot, uint capacity, vec3 lin_delta, vec3 ang_delta) {
    uint p = pos_of.v[slot];
    if (p >= capacity) return;

    scratch_values.v[kChanLinX * capacity + p] = lin_delta.x;
    scratch_values.v[kChanLinY * capacity + p] = lin_delta.y;
    scratch_values.v[kChanLinZ * capacity + p] = lin_delta.z;
    scratch_values.v[kChanAngX * capacity + p] = ang_delta.x;
    scratch_values.v[kChanAngY * capacity + p] = ang_delta.y;
    scratch_values.v[kChanAngZ * capacity + p] = ang_delta.z;
    scratch_values.v[kChanFlag * capacity + p] = 1.0f;
}

#endif // XPBD_SCATTER_GLSL
