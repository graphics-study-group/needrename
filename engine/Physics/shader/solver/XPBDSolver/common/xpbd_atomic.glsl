// xpbd_atomic.glsl — Macros for portable atomic float-add on SSBO int[].
//
// Usage: ATOMIC_ADD_FLOAT(some_int_ssbo.v[index], float_value);
//
// Expands to an atomicCompSwap loop that performs float addition via
// IEEE-754 bit manipulation.

#ifndef XPBD_ATOMIC_GLSL
#define XPBD_ATOMIC_GLSL

#define ATOMIC_ADD_FLOAT(mem_ref, val) \
{ \
    int _exp_ = (mem_ref); \
    for (;;) { \
        float _nv_ = intBitsToFloat(_exp_) + (val); \
        int _nb_ = floatBitsToInt(_nv_); \
        int _old_ = atomicCompSwap((mem_ref), _exp_, _nb_); \
        if (_old_ == _exp_) { break; } \
        _exp_ = _old_; \
    } \
}

#endif // XPBD_ATOMIC_GLSL
