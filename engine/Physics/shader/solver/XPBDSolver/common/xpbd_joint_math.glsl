// xpbd_joint_math.glsl — Shared math helpers for joint constraint shaders.
//
// Provides:
//   - quat_inverse          : quaternion conjugate (inverse for unit quaternions)
//   - quat_to_rotation_vec  : quaternion to rotation vector (2 * imag part)
//
// Requires xpbd_math.glsl for quat_rotate, quat_inv_rotate, quat_mul,
// apply_world_inv_inertia.

#ifndef XPBD_JOINT_MATH_GLSL
#define XPBD_JOINT_MATH_GLSL

/// Conjugate of a quaternion (inverse for unit quaternions).
vec4 quat_inverse(vec4 q) {
    return vec4(-q.x, -q.y, -q.z, q.w);
}

/// Convert a quaternion to a rotation vector (scaled axis-angle).
/// For small rotations near identity, this is approximately 2 * q.xyz.
vec3 quat_to_rotation_vec(vec4 q) {
    // Ensure we use the shortest path: flip sign if w < 0.
    float sign = (q.w >= 0.0) ? 1.0 : -1.0;
    return 2.0 * sign * q.xyz;
}

#endif // XPBD_JOINT_MATH_GLSL
