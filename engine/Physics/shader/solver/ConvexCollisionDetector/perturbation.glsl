// perturbation.glsl — Contact manifold generation via perturbation method.
//
// After MPR finds the base contact normal and a single contact point, this
// module expands the result into a contact manifold:
//
//   1. Compute two orthogonal axes u, v perpendicular to the contact normal.
//   2. Generate 6 perturbed directions (every 60° around the normal, tilted 2°
//      off the contact plane).
//   3. For each perturbed direction, query support() on both shapes and project
//      the result onto the 2D contact plane.
//   4. Clip polygon A by polygon B (Sutherland-Hodgman).
//   5. Reduce to at most 4 vertices (rotating calipers).
//   6. Un-project each vertex to world space on each shape's surface.
//
// Returns up to 4 contact point pairs on shapes A and B.

#ifndef CONVEX_COLLISION_PERTURBATION_GLSL
#define CONVEX_COLLISION_PERTURBATION_GLSL

#include "clipping.glsl"

const float PERTURB_TILT_ANGLE = 2.0; // degrees off the contact plane

// ---------------------------------------------------------------------------
// Compute two orthogonal axes perpendicular to a normal vector.
// Handles the degenerate case where the normal is near (0, 0, 1) or (0, 0, -1).
// ---------------------------------------------------------------------------

void compute_orthogonal_axes(vec3 n, out vec3 u, out vec3 v) {
    vec3 ref = (abs(n.z) < 0.9) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    u = normalize(cross(n, ref));
    v = cross(n, u);
}

// ---------------------------------------------------------------------------
// Perturbation manifold result.
// ---------------------------------------------------------------------------

struct PerturbResult {
    vec3 contact_points_a[4];  // world-space on shape A
    vec3 contact_points_b[4];  // world-space on shape B
    uint point_count;          // 0-4 (0 = clipping failed)
};

// ---------------------------------------------------------------------------
// Perturbation manifold generation.
//
// Parameters:
//   shape_a, shape_b — shape indices
//   contact_normal   — world-space normal from MPR (pointing B→A)
//   contact_center   — midpoint between MPR contact points
//   penetration      — MPR penetration depth
// ---------------------------------------------------------------------------

PerturbResult perturb_manifold(
    uint shape_a,
    uint shape_b,
    vec3 contact_normal,
    vec3 contact_center,
    float penetration
) {
    PerturbResult result;
    result.point_count = 0u;
    float half_depth = penetration * 0.5;

    // Step 1: Orthogonal axes on the contact plane.
    vec3 u, v;
    compute_orthogonal_axes(contact_normal, u, v);

    // Step 2: Generate 6 perturbed directions, collect 2D projected points.
    const float tilt = tan(radians(PERTURB_TILT_ANGLE));
    const float deg60 = radians(60.0);

    vec2 poly_a[6];
    vec2 poly_b[6];

    for (int i = 0; i < 6; i++) {
        float angle = float(i) * deg60;
        float ca = cos(angle);
        float sa = sin(angle);
        vec3 dir = normalize(ca * u + sa * v + tilt * contact_normal);

        vec3 world_a = support(shape_a, dir);
        vec3 world_b = support(shape_b, -dir);

        vec3 offset_a = world_a - contact_center;
        vec3 offset_b = world_b - contact_center;

        poly_a[i] = vec2(dot(offset_a, u), dot(offset_a, v));
        poly_b[i] = vec2(dot(offset_b, u), dot(offset_b, v));
    }

    // Step 3: Sutherland-Hodgman clipping.
    ClipResult clip_res = sutherland_hodgman_clip(poly_a, 6u, poly_b, 6u);
    if (clip_res.vertex_count == 0u) {
        return result;
    }

    // Step 4: Reduce to at most 4 vertices.
    uint final_count;
    vec2 final_verts[4];

    if (clip_res.vertex_count > 4u) {
        final_count = rotating_calipers_reduce(clip_res.vertices, clip_res.vertex_count, final_verts);
    } else {
        final_count = clip_res.vertex_count;
        for (uint j = 0u; j < clip_res.vertex_count; j++) {
            final_verts[j] = clip_res.vertices[j];
        }
    }

    // Step 5: Un-project each 2D vertex to world space on each shape.
    for (uint k = 0u; k < final_count && k < 4u; k++) {
        vec3 world_on_plane = contact_center + final_verts[k].x * u + final_verts[k].y * v;
        result.contact_points_a[k] = world_on_plane + contact_normal * half_depth;
        result.contact_points_b[k] = world_on_plane - contact_normal * half_depth;
    }
    result.point_count = final_count;

    return result;
}

#endif // CONVEX_COLLISION_PERTURBATION_GLSL
