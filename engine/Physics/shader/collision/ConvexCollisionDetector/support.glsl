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
// Shape type constants
// ---------------------------------------------------------------------------

#define SHAPE_TYPE_BOX      0u
#define SHAPE_TYPE_SPHERE   1u
#define SHAPE_TYPE_CYLINDER 2u

// ---------------------------------------------------------------------------
// Sphere support function
// ---------------------------------------------------------------------------

/// Returns the world-space support point of a sphere shape in direction dir_world.
/// The sphere is defined by its feature vec4 (radius in x), world position, and
/// world rotation (ignored — sphere is rotationally invariant).
vec3 support_sphere(
    vec4 feature,
    vec3 world_position,
    vec4 world_rotation,
    vec3 dir_world
) {
    float r = feature.x;
    float len_dir = length(dir_world);
    if (len_dir < 1e-8) {
        return world_position;
    }
    return world_position + (dir_world / len_dir) * r;
}

// ---------------------------------------------------------------------------
// Cylinder support function (Z-up)
// ---------------------------------------------------------------------------

/// Returns the world-space support point of a Z-up cylinder shape in direction
/// dir_world.  The cylinder is defined by its feature vec4 (radius in x,
/// half-height in y), world position, and world rotation.
vec3 support_cylinder(
    vec4 feature,
    vec3 world_position,
    vec4 world_rotation,
    vec3 dir_world
) {
    float r = feature.x;
    float half_h = feature.y;

    // Transform direction to local space.
    vec3 dir_local = quat_inv_rotate(world_rotation, dir_world);

    // Z (axial) component.
    float z_support = (dir_local.z >= 0.0) ? half_h : -half_h;

    // XY (radial) component.
    vec2 dir_xy = vec2(dir_local.x, dir_local.y);
    float len_xy = length(dir_xy);
    vec2 radial = (len_xy > 1e-8) ? (dir_xy / len_xy) * r : vec2(0.0);

    vec3 local_support = vec3(radial.x, radial.y, z_support);
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
//   ShapeFeature       — binding 2, readonly, vec4
//   ShapeWorldPosition — binding 3, readonly, vec4
//   ShapeWorldRotation — binding 4, readonly, vec4

// (The actual buffer declarations live in the main .comp file.  The support
//  functions reference them by name.)

/// Main dispatch: returns the world-space support point for shape_index in
/// direction dir_world.  Switches on shape_type.
vec3 support(uint shape_index, vec3 dir_world);

vec3 support(uint shape_index, vec3 dir_world) {
    uint st = shape_type.v[shape_index];

    if (st == SHAPE_TYPE_BOX) {
        return support_box(
            shape_feature.v[shape_index].xyz,
            shape_world_position.v[shape_index].xyz,
            shape_world_rotation.v[shape_index],
            dir_world
        );
    }

    if (st == SHAPE_TYPE_SPHERE) {
        return support_sphere(
            shape_feature.v[shape_index],
            shape_world_position.v[shape_index].xyz,
            shape_world_rotation.v[shape_index],
            dir_world
        );
    }

    if (st == SHAPE_TYPE_CYLINDER) {
        return support_cylinder(
            shape_feature.v[shape_index],
            shape_world_position.v[shape_index].xyz,
            shape_world_rotation.v[shape_index],
            dir_world
        );
    }

    // Unknown shape type — return world position as fallback.
    return shape_world_position.v[shape_index].xyz;
}

#endif // CONVEX_COLLISION_SUPPORT_GLSL
