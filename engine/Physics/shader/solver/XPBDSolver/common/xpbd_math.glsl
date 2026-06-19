// xpbd_math.glsl — Shared math utilities for XPBD compute shaders.
//
// Provides:
//   - quat_rotate, quat_inv_rotate   : quaternion vector rotation
//   - vec3_dot, vec3_cross           : basic vector operations
//   - apply_world_inv_inertia        : world→local→(inv inertia)→world
//   - quat_normalize                 : normalize a quaternion
//   - multiply_mat4_3x3_vec3         : extract 3x3 from mat4 × vec3

#ifndef XPBD_MATH_GLSL
#define XPBD_MATH_GLSL

// ---------------------------------------------------------------------------
// Quaternion rotation helpers (match support.glsl conventions)
// ---------------------------------------------------------------------------

/// Rotate vector v by quaternion q (assumes q is unit length).
/// q.xyz = imaginary part, q.w = real part.
vec3 quat_rotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

/// Inverse-rotate vector v by quaternion q (rotate by conjugate).
vec3 quat_inv_rotate(vec4 q, vec3 v) {
    // q_inv = vec4(-q.xyz, q.w) for unit quaternion
    vec3 t = 2.0 * cross(-q.xyz, v);
    return v + q.w * t + cross(-q.xyz, t);
}

// ---------------------------------------------------------------------------
// Basic vector operations
// ---------------------------------------------------------------------------

float vec3_dot(vec3 a, vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3 vec3_cross(vec3 a, vec3 b) {
    return vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

// ---------------------------------------------------------------------------
// Quaternion normalization
// ---------------------------------------------------------------------------

vec4 quat_normalize(vec4 q) {
    float len = sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 1e-12) {
        return q / len;
    }
    return vec4(0.0, 0.0, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// apply_world_inv_inertia
//
// Transforms a world-space vector through the body's local inverse inertia
// tensor, accounting for the current orientation.
//
//   local_inv_inertia : mat4 where the upper-left 3x3 is the body-frame
//                       inverse inertia tensor.
//   orientation       : body world orientation quaternion (vec4, xyzw).
//   world_vec         : input vector in world space.
//
// Returns: I_world^{-1} * world_vec  (in world space).
// ---------------------------------------------------------------------------

vec3 apply_world_inv_inertia(mat4 local_inv_inertia, vec4 orientation, vec3 world_vec) {
    // Transform to body-local space.
    vec3 local_vec = quat_inv_rotate(orientation, world_vec);

    // Multiply by the 3x3 inverse inertia in local space.
    // The upper-left 3x3 of the mat4 is extracted as a mat3.
    mat3 inv_I = mat3(local_inv_inertia);
    vec3 local_result = inv_I * local_vec;

    // Transform back to world space.
    return quat_rotate(orientation, local_result);
}

// ---------------------------------------------------------------------------
// multiply_mat4_3x3_vec3
//
// Multiplies only the upper-left 3x3 of a mat4 by a vec3.
// Used by the Coriolis term where we need I_local * wb (not the inverse).
// ---------------------------------------------------------------------------

vec3 multiply_mat4_3x3_vec3(mat4 local_inertia, vec3 v) {
    mat3 I = mat3(local_inertia);
    return I * v;
}

#endif // XPBD_MATH_GLSL
