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

    if (subject_count < 3u || clip_count < 3u) {
        return result;
    }

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
// Rotating calipers — select 4 vertices with maximum quadrilateral area.
//
// For a small polygon (at most MAX_CLIP_VERTS vertices), we brute-force all
// C(N,4) combinations to find the maximum-area quadrilateral.  This is O(N^4)
// but perfectly adequate for N <= 16 (C(16,4) = 1820 combinations).
// ---------------------------------------------------------------------------

float quad_area(vec2 a, vec2 b, vec2 c, vec2 d) {
    // Shoelace formula for quadrilateral a→b→c→d (assumed convex, CCW order).
    return 0.5 * abs(
        a.x * b.y + b.x * c.y + c.x * d.y + d.x * a.y -
        a.y * b.x - b.y * c.x - c.y * d.x - d.y * a.x
    );
}

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

    float best_area = -1.0;
    uint best_i = 0u, best_j = 1u, best_k = 2u, best_l = 3u;

    // Brute-force search over all 4-vertex subsets.
    // The polygon is convex and the vertices are in CCW order, so any subset
    // in order forms a convex quadrilateral.
    for (uint i = 0u; i < vertex_count; i++) {
        for (uint j = i + 1u; j < vertex_count; j++) {
            for (uint k = j + 1u; k < vertex_count; k++) {
                for (uint l = k + 1u; l < vertex_count; l++) {
                    float area = quad_area(
                        vertices[i], vertices[j], vertices[k], vertices[l]
                    );
                    if (area > best_area) {
                        best_area = area;
                        best_i = i; best_j = j; best_k = k; best_l = l;
                    }
                }
            }
        }
    }

    result_verts[0] = vertices[best_i];
    result_verts[1] = vertices[best_j];
    result_verts[2] = vertices[best_k];
    result_verts[3] = vertices[best_l];

    return 4u;
}

#endif // CONVEX_COLLISION_CLIPPING_GLSL
