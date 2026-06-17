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
//   4. The resulting 2D polygons are passed to the clipping module.
//
// Returns a centroid contact point in world space (average of the clipped
// polygon vertices, un-projected back to 3D).

#ifndef CONVEX_COLLISION_PERTURBATION_GLSL
#define CONVEX_COLLISION_PERTURBATION_GLSL

#include "clipping.glsl"

const float PERTURB_TILT_ANGLE = 2.0; // degrees off the contact plane

// ---------------------------------------------------------------------------
// Compute two orthogonal axes perpendicular to a normal vector.
// Handles the degenerate case where the normal is near (0, 0, 1) or (0, 0, -1).
// ---------------------------------------------------------------------------

void compute_orthogonal_axes(vec3 n, out vec3 u, out vec3 v) {
    // Pick a reference vector that is not parallel to n.
    vec3 ref = (abs(n.z) < 0.9) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    u = normalize(cross(n, ref));
    v = cross(n, u); // already unit-length since n and u are orthogonal and unit
}

// ---------------------------------------------------------------------------
// Perturbation manifold generation.
//
// Parameters:
//   shape_a, shape_b — shape indices
//   contact_normal   — world-space normal from MPR (pointing B→A)
//   contact_center   — world-space contact point from MPR (on the contact plane)
//
// Returns:
//   centroid — averaged contact point in world space (on the contact plane)
//   clip_vertex_count — number of vertices in the final clipped polygon
// ---------------------------------------------------------------------------

struct PerturbResult {
    vec3 contact_centroid;     // world-space centroid of the contact region
    uint clip_vertex_count;    // number of vertices in the clipped polygon
};

PerturbResult perturb_manifold(
    uint shape_a,
    uint shape_b,
    vec3 contact_normal,
    vec3 contact_center
) {
    PerturbResult result;
    result.contact_centroid = contact_center;
    result.clip_vertex_count = 0u;

    // Step 1: Orthogonal axes on the contact plane.
    vec3 u, v;
    compute_orthogonal_axes(contact_normal, u, v);

    // Step 2: Generate 6 perturbed directions and collect 2D projected points.
    //
    // Each direction is tilted 2° off the contact plane toward the contact
    // normal, rotated every 60° around the normal.
    //
    //   d = cos(θ)*u + sin(θ)*v + tan(2°)*n   (normalized)

    const float tilt = tan(radians(PERTURB_TILT_ANGLE));
    const float deg60 = radians(60.0);

    vec2 poly_a[6];
    vec2 poly_b[6];

    for (int i = 0; i < 6; i++) {
        float angle = float(i) * deg60;
        float ca = cos(angle);
        float sa = sin(angle);

        vec3 dir = normalize(ca * u + sa * v + tilt * contact_normal);

        // Support points in world space.
        vec3 world_a = support(shape_a, dir);
        vec3 world_b = support(shape_b, -dir);

        // Project onto the 2D contact plane.
        vec3 offset_a = world_a - contact_center;
        vec3 offset_b = world_b - contact_center;

        poly_a[i] = vec2(dot(offset_a, u), dot(offset_a, v));
        poly_b[i] = vec2(dot(offset_b, u), dot(offset_b, v));
    }

    // Step 3: Sutherland-Hodgman clipping — clip polygon A by edges of polygon B.
    //
    // We clip the 6-vertex polygon A by the 6 edges of polygon B.
    ClipResult clip_res = sutherland_hodgman_clip(poly_a, 6u, poly_b, 6u);
    result.clip_vertex_count = clip_res.vertex_count;

    if (clip_res.vertex_count == 0u) {
        // Clipping produced no intersection — fall back to the MPR contact point.
        return result;
    }

    // Step 4: Rotating calipers — reduce to at most 4 vertices.
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

    // Step 5: Compute centroid in 2D and un-project to world space.
    vec2 centroid_2d = vec2(0.0);
    for (uint k = 0u; k < final_count; k++) {
        centroid_2d += final_verts[k];
    }
    centroid_2d /= float(final_count);

    // Un-project: world = contact_center + centroid_2d.x * u + centroid_2d.y * v
    result.contact_centroid = contact_center + centroid_2d.x * u + centroid_2d.y * v;

    return result;
}

#endif // CONVEX_COLLISION_PERTURBATION_GLSL
