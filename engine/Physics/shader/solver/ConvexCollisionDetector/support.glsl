// support.glsl — Support function interface for convex collision shapes.
//
// Every convex shape implements a support function that returns the farthest
// point in world space along a given direction.  This file provides:
//   - quaternion rotation helpers
//   - the box support function
//   - a dispatch function that switches on shape_type

#ifndef CONVEX_COLLISION_SUPPORT_GLSL
#define CONVEX_COLLISION_SUPPORT_GLSL

// ---------------------------------------------------------------------------
// Quaternion helpers
// ---------------------------------------------------------------------------

/// Rotate vector v by quaternion q (assumes q is unit length).
/// q.xyz = imaginary part, q.w = real part.
vec3 quat_rotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

/// Inverse-rotate vector v by quaternion q (equivalent to rotate by conjugate).
vec3 quat_inv_rotate(vec4 q, vec3 v) {
    return quat_rotate(vec4(-q.xyz, q.w), v);
}

// ---------------------------------------------------------------------------
// Box support function
// ---------------------------------------------------------------------------

/// Returns the world-space support point of a box shape in direction dir_world.
/// The box is defined by its world position, world rotation quaternion, and
/// half-extents in local space.
vec3 support_box(
    vec3 half_extents,
    vec3 world_position,
    vec4 world_rotation,
    vec3 dir_world
) {
    // Transform direction to local space.
    vec3 dir_local = quat_inv_rotate(world_rotation, dir_world);

    // Support in local space: sign of direction component * half_extent.
    vec3 local_support = vec3(
        (dir_local.x >= 0.0 ? half_extents.x : -half_extents.x),
        (dir_local.y >= 0.0 ? half_extents.y : -half_extents.y),
        (dir_local.z >= 0.0 ? half_extents.z : -half_extents.z)
    );

    // Transform back to world space.
    return quat_rotate(world_rotation, local_support) + world_position;
}

// ---------------------------------------------------------------------------
// Shape data accessors
// ---------------------------------------------------------------------------

// These buffers are bound by the main compute shader.  The support functions
// access them through these declarations.  Each includer must declare the
// buffers with these exact names and bindings.

// Expected bindings (set = 0):
//   ShapeAlive         — binding 0, readonly, uint
//   ShapeType          — binding 1, readonly, uint
//   ShapeHalfExtents   — binding 2, readonly, vec4
//   ShapeWorldPosition — binding 3, readonly, vec4
//   ShapeWorldRotation — binding 4, readonly, vec4

// (The actual buffer declarations live in the main .comp file.  The support
//  functions reference them by name.)

/// Main dispatch: returns the world-space support point for shape_index in
/// direction dir_world.  Switches on shape_type.
vec3 support(uint shape_index, vec3 dir_world);

vec3 support(uint shape_index, vec3 dir_world) {
    // shape_type: 0 = Box
    uint st = shape_type.v[shape_index];

    // Box
    if (st == 0u) {
        return support_box(
            shape_half_extents.v[shape_index].xyz,
            shape_world_position.v[shape_index].xyz,
            shape_world_rotation.v[shape_index],
            dir_world
        );
    }

    // Unknown shape type — return world position as fallback.
    return shape_world_position.v[shape_index].xyz;
}

#endif // CONVEX_COLLISION_SUPPORT_GLSL
