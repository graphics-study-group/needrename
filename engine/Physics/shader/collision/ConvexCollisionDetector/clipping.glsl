// clipping.glsl — 2D convex polygon clipping and reduction.
//
// Provides:
//   - Sutherland-Hodgman clipping (clip subject polygon by convex clip polygon)
//   - Rotating calipers reduction to max 4 vertices (maximum area quadrilateral)
//
// Both algorithms operate in 2D (the contact plane).

#ifndef CONVEX_COLLISION_CLIPPING_GLSL
#define CONVEX_COLLISION_CLIPPING_GLSL

const uint MAX_CLIP_VERTS = 16u; // maximum possible output vertices from S-H
const float CLIP_EPSILON = 1e-6; // tolerance for degenerate edges/vertices

struct ClipResult {
    vec2 vertices[MAX_CLIP_VERTS];
    uint vertex_count;
};

// ---------------------------------------------------------------------------
// Sutherland-Hodgman polygon clipping.
//
// Clips the convex subject polygon by each edge of the convex clip polygon.
// Both polygons are assumed convex and CCW-ordered.
//
// Each edge of the clip polygon defines a half-plane (interior is to the left
// of the edge direction when walking CCW).  Vertices of the subject polygon
// are classified as inside or outside each half-plane.
// ---------------------------------------------------------------------------

/// Signed area of triangle (a,b,c).  Positive = CCW.
float signed_area_2d(vec2 a, vec2 b, vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/// Distance of point p from the directed line ab (positive = left / inside for CCW).
float line_distance_2d(vec2 a, vec2 b, vec2 p) {
    return signed_area_2d(a, b, p);
    // Not normalised — we only care about the sign, not the magnitude.
}

/// Intersection of line segment p1→p2 with clip edge a→b.
/// Assumes p1 is inside and p2 is outside (or vice versa).
vec2 line_intersection_2d(vec2 a, vec2 b, vec2 p1, vec2 p2) {
    vec2 d1 = b - a;
    vec2 d2 = p2 - p1;
    float cross = d1.x * d2.y - d1.y * d2.x;

    if (abs(cross) < 1e-10) {
        return p2; // parallel — just return the endpoint
    }

    float t = ((p1.x - a.x) * d2.y - (p1.y - a.y) * d2.x) / cross;
    return a + t * d1;
}

ClipResult sutherland_hodgman_clip(
    vec2 subject[6],
    uint subject_count,
    vec2 clip_poly[6],
    uint clip_count
) {
    ClipResult result;
    result.vertex_count = 0u;

    if (subject_count <= 1u) {
        result.vertex_count = 1u;
        result.vertices[0] = subject[0];
        return result;
    } else if (clip_count <= 1u) {
        result.vertex_count = 1u;
        result.vertices[0] = clip_poly[0];
        return result;
    } else if (subject_count == 2u && clip_count == 2u) {
        // Special case for line segment vs line segment — just return the intersection point (if any).
        result.vertex_count = 1u;
        result.vertices[0] = line_intersection_2d(subject[0], subject[1], clip_poly[0], clip_poly[1]);
        return result;
    } else if (subject_count == 2u) {
        // Special case for line segment — just clip to each edge and return the
        // single remaining point (if any).
        vec2 p1 = subject[0];
        vec2 p2 = subject[1];
        for (uint e = 0u; e < clip_count; e++) {
            uint next_e = (e + 1u) % clip_count;
            vec2 ca = clip_poly[e];
            vec2 cb = clip_poly[next_e];

            float d1 = line_distance_2d(ca, cb, p1);
            float d2 = line_distance_2d(ca, cb, p2);

            if (d1 < 0.0 && d2 < 0.0) {
                // both outside — skip both (impossible)
                return result;
            } else if (d1 >= 0.0 && d2 < 0.0) {
                // p1 inside, p2 outside — keep intersection.
                p2 = line_intersection_2d(ca, cb, p1, p2);
            } else if (d1 < 0.0 && d2 >= 0.0) {
                // p1 outside, p2 inside — keep intersection + p2.
                p1 = line_intersection_2d(ca, cb, p1, p2);
            }
            // else: Both points inside — keep both.
        }
        result.vertex_count = 2u;
        result.vertices[0] = p1;
        result.vertices[1] = p2;
        return result;
    } else if (clip_count == 2u) {
        // Special case for line segment — just clip to each edge and return the
        // single remaining point (if any).
        vec2 p1 = clip_poly[0];
        vec2 p2 = clip_poly[1];
        for (uint e = 0u; e < subject_count; e++) {
            uint next_e = (e + 1u) % subject_count;
            vec2 ca = subject[e];
            vec2 cb = subject[next_e];

            float d1 = line_distance_2d(ca, cb, p1);
            float d2 = line_distance_2d(ca, cb, p2);

            if (d1 < 0.0 && d2 < 0.0) {
                // both outside — skip both (impossible)
                return result;
            } else if (d1 >= 0.0 && d2 < 0.0) {
                // p1 inside, p2 outside — keep intersection.
                p2 = line_intersection_2d(ca, cb, p1, p2);
            } else if (d1 < 0.0 && d2 >= 0.0) {
                // p1 outside, p2 inside — keep intersection + p2.
                p1 = line_intersection_2d(ca, cb, p1, p2);
            }
            // else: Both points inside — keep both.
        }
        result.vertex_count = 2u;
        result.vertices[0] = p1;
        result.vertices[1] = p2;
        return result;
    }

    // both subject and clip are polygons with at least 3 vertices — perform full S-H clipping.

    // Copy subject into working buffer.
    vec2 input_list[MAX_CLIP_VERTS];
    vec2 output_list[MAX_CLIP_VERTS];
    uint input_count = subject_count;

    for (uint i = 0u; i < subject_count; i++) {
        input_list[i] = subject[i];
    }

    // For each edge of the clip polygon...
    for (uint e = 0u; e < clip_count; e++) {
        uint next_e = (e + 1u) % clip_count;
        vec2 ca = clip_poly[e];
        vec2 cb = clip_poly[next_e];

        uint output_count = 0u;

        if (input_count == 0u) {
            break;
        }

        for (uint i = 0u; i < input_count; i++) {
            uint next_i = (i + 1u) % input_count;
            vec2 current = input_list[i];
            vec2 next_v = input_list[next_i];

            float d_current = line_distance_2d(ca, cb, current);
            float d_next = line_distance_2d(ca, cb, next_v);

            bool current_inside = (d_current >= 0.0);
            bool next_inside = (d_next >= 0.0);

            if (current_inside) {
                // Current point is inside — keep it.
                output_list[output_count] = current;
                output_count++;

                if (!next_inside) {
                    // Exiting the clip region — add intersection point.
                    output_list[output_count] = line_intersection_2d(ca, cb, current, next_v);
                    output_count++;
                }
            } else if (next_inside) {
                // Entering the clip region — add intersection + next point.
                output_list[output_count] = line_intersection_2d(ca, cb, current, next_v);
                output_count++;
            }
            // else: both outside — skip both.

            if (output_count >= MAX_CLIP_VERTS) {
                break;
            }
        }

        // Swap buffers for next clip edge.
        for (uint j = 0u; j < output_count; j++) {
            input_list[j] = output_list[j];
        }
        input_count = output_count;

        if (input_count < 3u) {
            // Degenerate — no intersection.
            result.vertex_count = 0u;
            return result;
        }
    }

    // Output the final polygon.
    result.vertex_count = input_count;
    for (uint k = 0u; k < input_count && k < MAX_CLIP_VERTS; k++) {
        result.vertices[k] = input_list[k];
    }

    return result;
}

// ---------------------------------------------------------------------------
// Rotating calipers — select 4 vertices forming the approximate
// maximum-area quadrilateral from a convex CCW polygon in O(N) time.
//
// Phase 1: Walk antipodal pairs to find the hull's diameter (p1, p3).
// Phase 2: Single-pass scan to find extreme points on both sides (p2, p4).
//
// Uses relative tie-breaking to handle near-circular geometry deterministically.
// ---------------------------------------------------------------------------

uint rotating_calipers_reduce(
    vec2 vertices[MAX_CLIP_VERTS],
    uint vertex_count,
    out vec2 result_verts[4]
) {
    if (vertex_count <= 4u) {
        for (uint i = 0u; i < vertex_count; i++) {
            result_verts[i] = vertices[i];
        }
        return vertex_count;
    }

    // Relative epsilon for tie-breaking: only update if new value is at least
    // (1 + epsilon) times better.  Scale-invariant — avoids catastrophic
    // cancellation in floating-point comparisons.  Important for circular or
    // symmetric geometry to ensure deterministic point selection.
    const float tie_epsilon_rel = 1.0e-3;

    // ---- Phase 1: Find the hull's diameter using Rotating Calipers in O(N) ----
    //
    // Walk antipodal pairs around the hull.  For each edge (i, i+1), advance
    // the antipodal pointer j while the next vertex j+1 is further from the edge.
    // Both (i, j) and (i+1, j) are antipodal pairs — track the one with maximum
    // squared distance.

    uint p1 = 0u;
    uint p3 = 1u;
    vec2 d_init = vertices[0u] - vertices[1u];
    float max_dist_sq = d_init.x * d_init.x + d_init.y * d_init.y;

    uint j = 1u; // antipodal pointer, starts opposite i=0
    for (uint i = 0u; i < vertex_count; i++) {
        uint i_next = (i + 1u) % vertex_count;
        vec2 hull_i = vertices[i];
        vec2 hull_i_next = vertices[i_next];

        // Advance j while the area of triangle (i, i+1, j+1) exceeds
        // that of (i, i+1, j).  This finds the vertex furthest from edge (i, i+1).
        // Use a counted loop (max N iterations total across all i) to avoid
        // unbounded while-loop concerns on GPU compilers.
        for (uint step = 0u; step < vertex_count; step++) {
            uint j_next = (j + 1u) % vertex_count;
            float area_j      = signed_area_2d(hull_i, hull_i_next, vertices[j]);
            float area_j_next = signed_area_2d(hull_i, hull_i_next, vertices[j_next]);

            if (area_j_next > area_j) {
                j = j_next;
            } else {
                break;
            }
        }

        // Check antipodal pair (i, j).
        {
            vec2 hi = vertices[i];
            vec2 hj = vertices[j];
            vec2 d = hi - hj;
            float dist_sq = d.x * d.x + d.y * d.y;
            if (dist_sq > max_dist_sq * (1.0 + tie_epsilon_rel)) {
                max_dist_sq = dist_sq;
                p1 = i;
                p3 = j;
            }
        }

        // Check antipodal pair (i+1, j).
        {
            vec2 hi_next = vertices[i_next];
            vec2 hj = vertices[j];
            vec2 d = hi_next - hj;
            float dist_sq = d.x * d.x + d.y * d.y;
            if (dist_sq > max_dist_sq * (1.0 + tie_epsilon_rel)) {
                max_dist_sq = dist_sq;
                p1 = i_next;
                p3 = j;
            }
        }
    }

    // ---- Phase 2: Find points p2 and p4 furthest from the diameter (p1, p3) ----
    //
    // Single O(N) scan using signed area to determine which side of the
    // diameter line each vertex falls on.

    uint p2 = 0u;
    uint p4 = 0u;
    float max_area_1 = 0.0;
    float max_area_2 = 0.0;

    vec2 hull_p1 = vertices[p1];
    vec2 hull_p3 = vertices[p3];

    for (uint i = 0u; i < vertex_count; i++) {
        float area = signed_area_2d(hull_p1, hull_p3, vertices[i]);

        // Use relative tie-breaking: only update if new area is meaningfully larger.
        if (area > max_area_1 * (1.0 + tie_epsilon_rel)) {
            max_area_1 = area;
            p2 = i;
        } else if (-area > max_area_2 * (1.0 + tie_epsilon_rel)) {
            max_area_2 = -area;
            p4 = i;
        }
    }

    result_verts[0] = vertices[p1];
    result_verts[1] = vertices[p2];
    result_verts[2] = vertices[p3];
    result_verts[3] = vertices[p4];

    return 4u;
}

#endif // CONVEX_COLLISION_CLIPPING_GLSL
