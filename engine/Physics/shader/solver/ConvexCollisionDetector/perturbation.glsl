// perturbation.glsl — Contact manifold generation via perturbation method.
//
// After MPR finds the base contact normal and a single contact point, this
// module expands the result into a contact manifold:
//
//   1. Compute two orthogonal axes u, v perpendicular to the contact normal.
//   2. Generate 6 perturbed directions (every 60° around the normal, tilted 2°
//      off the contact plane).
//   3. For each perturbed direction, query support() on both shapes, update
//      an incremental plane tracker with the world-space points, and project
//      the result onto the 2D contact plane.
//   4. Fit a contact plane for each shape using the incremental tracker
//      (largest triangle normal, centroid origin).
//   5. Clip polygon A by polygon B (Sutherland-Hodgman).
//   6. Reduce to at most 4 vertices (rotating calipers).
//   7. Un-project each vertex to 3D by ray-casting from the MPR contact plane
//      along the MPR normal onto each shape's fitted plane.
//   8. Validate each contact point: compute per-point depth; discard points
//      whose separation exceeds contact_margin.
//   9. If the fitted plane normals diverge by more than 0.1°, add MPR deepest
//      point. If total exceeds 4, reduce via rotating calipers.
//
// Returns up to 4 contact point pairs on shapes A and B, each with its own
// independently computed penetration depth.

#ifndef CONVEX_COLLISION_PERTURBATION_GLSL
#define CONVEX_COLLISION_PERTURBATION_GLSL

#include "clipping.glsl"

const float PERTURB_TILT_ANGLE = 2.0; // degrees off the contact plane
const float SIN_TILT_ANGLE = sin(radians(PERTURB_TILT_ANGLE));
const float COS_TILT_ANGLE = cos(radians(PERTURB_TILT_ANGLE));

// When dot(normal_A, normal_B) < this, the contact surfaces are considered
// significantly non-parallel and the MPR deepest point is added as a fallback.
const float PLANE_ALIGNMENT_EPSILON = 0.9999985; // cos(radians(0.1))

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
// Incremental plane tracker.
//
// Maintains the largest-area triangle found so far among a stream of points.
// After processing all N points (N ≤ 6), the tracked normal is the normal of
// the dominant planar region.  O(N) single-pass, integrates directly into the
// perturbation point-collection loop.
// ---------------------------------------------------------------------------

struct IncrementalPlaneTracker {
    vec3 reference_point;   // first point (fixed)
    vec3 previous_point;    // most recently processed point
    vec3 normal;            // un-normalized cross product of winning triangle
    float largest_area_sq;  // |cross|² of winning triangle
};

void init_plane_tracker(out IncrementalPlaneTracker tracker, vec3 first_point) {
    tracker.reference_point = first_point;
    tracker.previous_point = first_point;
    tracker.normal = vec3(0.0);
    tracker.largest_area_sq = 0.0;
}

void update_plane_tracker(
    inout IncrementalPlaneTracker tracker,
    vec3 current_point,
    int current_point_id
) {
    if (current_point_id == 0) {
        tracker.reference_point = current_point;
        tracker.largest_area_sq = 0.0;
    } else if (current_point_id == 1) {
        tracker.previous_point = current_point;
    } else {
        vec3 edge1 = tracker.previous_point - tracker.reference_point;
        vec3 edge2 = current_point - tracker.reference_point;
        vec3 cross_prod = cross(edge1, edge2);
        float area_sq = dot(cross_prod, cross_prod);
        if (area_sq > tracker.largest_area_sq) {
            tracker.largest_area_sq = area_sq;
            tracker.normal = cross_prod;
        }
        tracker.previous_point = current_point;
    }
}

/// Finalise the tracker: normalise the winning normal.
/// Returns @p fallback_normal if largest_area_sq is zero (degenerate points).
vec3 finalize_plane_tracker(
    IncrementalPlaneTracker tracker,
    vec3 fallback_normal
) {
    if (tracker.largest_area_sq > 0.0) {
        return normalize(tracker.normal);
    }
    return fallback_normal;
}

// ---------------------------------------------------------------------------
// Ray-cast un-projection from the MPR contact plane to a fitted plane.
//
// Given a 2D vertex (u, v) on the MPR contact plane, construct the 3D point
// P = contact_center + u*u_axis + v*v_axis, then ray-cast along the MPR
// contact normal to intersect the fitted plane (defined by its own normal
// and origin).  This preserves the (u, v) coordinates exactly — the output
// point differs from the naive "world_on_plane ± half_depth" formula by a
// geometry-dependent offset along the MPR normal.
//
// If the ray is nearly parallel to the fitted plane (|dot(ray_dir, plane_normal)| < eps),
// returns P unchanged.
// ---------------------------------------------------------------------------

vec3 unproject_to_fitted_plane(
    vec2 vertex_2d,
    vec3 u_axis,
    vec3 v_axis,
    vec3 contact_center,
    vec3 ray_dir,          // MPR contact normal (unit length)
    vec3 plane_normal,     // fitted plane normal (unit length)
    vec3 plane_origin      // fitted plane origin (centroid)
) {
    vec3 P = contact_center + vertex_2d.x * u_axis + vertex_2d.y * v_axis;
    float denom = dot(ray_dir, plane_normal);
    if (abs(denom) < 1e-10) {
        return P; // degenerate — ray parallel to plane
    }
    float t = dot(plane_origin - P, plane_normal) / denom;
    return P + t * ray_dir;
}

// ---------------------------------------------------------------------------
// Perturbation manifold result.
// ---------------------------------------------------------------------------

struct PerturbResult {
    vec3 contact_points_a[4];  // world-space on shape A
    vec3 contact_points_b[4];  // world-space on shape B
    float penetrations[4];     // per-point penetration depth
    uint point_count;          // 0-4 (0 = use MPR fallback)
};

// ---------------------------------------------------------------------------
// Perturbation manifold generation.
//
// Parameters:
//   shape_a, shape_b  — shape indices
//   contact_normal    — world-space normal from MPR (pointing B→A)
//   contact_center    — midpoint between MPR contact points
//   penetration       — MPR penetration depth (fallback for MPR point)
//   contact_margin    — configurable tolerance; points with raw_depth > -margin
//                        are kept (speculative contacts report zero penetration)
//   mpr_point_a       — MPR contact point on A (for plane-alignment fallback)
//   mpr_point_b       — MPR contact point on B (for plane-alignment fallback)
// ---------------------------------------------------------------------------

PerturbResult perturb_manifold(
    uint shape_a,
    uint shape_b,
    vec3 contact_normal,
    vec3 contact_center,
    float penetration,
    float contact_margin,
    vec3 mpr_point_a,
    vec3 mpr_point_b
) {
    PerturbResult result;
    result.point_count = 0u;

    // Step 1: Orthogonal axes on the contact plane.
    vec3 u, v;
    compute_orthogonal_axes(contact_normal, u, v);

    // Step 2-3: Generate 6 perturbed directions, collect 2D projections
    // and track world-space points for plane fitting.
    const float deg60 = radians(60.0);

    vec2 poly_a[6];
    uint poly_a_count = 0u;
    vec2 poly_b[6];
    uint poly_b_count = 0u;

    // Plane trackers and centroid accumulators for each shape.
    IncrementalPlaneTracker tracker_a;
    IncrementalPlaneTracker tracker_b;
    vec3 centroid_sum_a = vec3(0.0);
    vec3 centroid_sum_b = vec3(0.0);
    const uint pert_point_count = 6u;

    for (int i = 0; i < 6; i++) {
        float angle = float(i) * deg60;
        float ca = cos(angle);
        float sa = sin(angle);
        vec3 dir = normalize(SIN_TILT_ANGLE * ca * u + SIN_TILT_ANGLE * sa * v + COS_TILT_ANGLE * contact_normal);

        vec3 world_a = support(shape_a, dir);
        vec3 world_b = support(shape_b, -dir);

        // Update incremental plane trackers.
        update_plane_tracker(tracker_a, world_a, i);
        update_plane_tracker(tracker_b, world_b, i);

        // Accumulate centroid sums.
        centroid_sum_a += world_a;
        centroid_sum_b += world_b;

        // 2D projection for clipping.
        vec3 offset_a = world_a - contact_center;
        vec3 offset_b = world_b - contact_center;

        vec2 projected_a = vec2(dot(offset_a, u), dot(offset_a, v));
        if (poly_a_count == 0u || length(projected_a - poly_a[poly_a_count - 1u]) > CLIP_EPSILON) {
            poly_a[poly_a_count] = projected_a;
            poly_a_count++;
        }
        vec2 projected_b = vec2(dot(offset_b, u), dot(offset_b, v));
        if (poly_b_count == 0u || length(projected_b - poly_b[poly_b_count - 1u]) > CLIP_EPSILON) {
            poly_b[poly_b_count] = projected_b;
            poly_b_count++;
        }
    }
    if (length(poly_a[poly_a_count - 1u] - poly_a[0u]) < CLIP_EPSILON) {
        poly_a_count--;
    }
    if (length(poly_b[poly_b_count - 1u] - poly_b[0u]) < CLIP_EPSILON) {
        poly_b_count--;
    }

    // Finalise plane trackers.
    vec3 centroid_a = centroid_sum_a / float(pert_point_count);
    vec3 centroid_b = centroid_sum_b / float(pert_point_count);
    vec3 fitted_normal_a = finalize_plane_tracker(tracker_a, contact_normal);
    vec3 fitted_normal_b = finalize_plane_tracker(tracker_b, contact_normal);

    // Step 5: Sutherland-Hodgman clipping.
    ClipResult clip_res = sutherland_hodgman_clip(poly_a, poly_a_count, poly_b, poly_b_count);
    if (clip_res.vertex_count == 0u) {
        return result; // point_count == 0 → MPR fallback
    }

    // Step 6: Reduce to at most 4 vertices.
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

    // Step 7-8: Un-project each 2D vertex to fitted planes, validate with
    // per-point depth and contact margin.
    vec3 valid_pts_a[4];
    vec3 valid_pts_b[4];
    vec2 valid_verts_2d[4]; // keep 2D coords for possible calipers re-run
    float valid_depths[4];
    uint valid_count = 0u;

    for (uint k = 0u; k < final_count && k < 4u; k++) {
        vec3 pt_a = unproject_to_fitted_plane(
            final_verts[k], u, v, contact_center,
            contact_normal, fitted_normal_a, centroid_a
        );
        vec3 pt_b = unproject_to_fitted_plane(
            final_verts[k], u, v, contact_center,
            contact_normal, fitted_normal_b, centroid_b
        );

        // Per-point raw depth: contact_normal points B→A, so
        // dot(pt_b - pt_a, contact_normal) is negative when shapes overlap.
        float raw_depth = -dot(pt_b - pt_a, contact_normal);

        // Validate with contact margin.
        if (raw_depth > -contact_margin) {
            valid_pts_a[valid_count] = pt_a;
            valid_pts_b[valid_count] = pt_b;
            valid_verts_2d[valid_count] = final_verts[k];
            valid_depths[valid_count] = max(raw_depth, 0.0);
            valid_count++;
        }
    }

    // Step 9: Plane-alignment fallback — if fitted planes are non-parallel,
    // add the MPR deepest point.
    if (dot(fitted_normal_a, fitted_normal_b) < PLANE_ALIGNMENT_EPSILON) {
        // Compute 2D projection of MPR contact midpoint for calipers.
        vec3 mpr_midpoint = (mpr_point_a + mpr_point_b) * 0.5;
        vec3 mpr_offset = mpr_midpoint - contact_center;
        vec2 mpr_2d = vec2(dot(mpr_offset, u), dot(mpr_offset, v));

        if (valid_count < 4u) {
            // Room to add directly.
            valid_pts_a[valid_count] = mpr_point_a;
            valid_pts_b[valid_count] = mpr_point_b;
            valid_verts_2d[valid_count] = mpr_2d;
            valid_depths[valid_count] = penetration;
            valid_count++;
        } else {
            // Already have 4 perturbation points — reduce combined set of 5
            // to the best 4 using rotating calipers.
            vec2 combined_2d[5];
            for (uint ci = 0u; ci < 4u; ci++) {
                combined_2d[ci] = valid_verts_2d[ci];
            }
            combined_2d[4] = mpr_2d;

            ClipResult combined_clip;
            combined_clip.vertex_count = 5u;
            for (uint ci = 0u; ci < 5u; ci++) {
                combined_clip.vertices[ci] = combined_2d[ci];
            }

            vec2 reduced_2d[4];
            uint reduced_count = rotating_calipers_reduce(combined_clip.vertices, 5u, reduced_2d);

            // Map reduced 2D vertices back to the original contact points.
            // For each reduced vertex, find the closest match among the 5 candidates.
            vec3 new_pts_a[4];
            vec3 new_pts_b[4];
            float new_depths[4];

            // Original 5 candidates: 4 perturbation + 1 MPR.
            vec3 all_pts_a[5];
            vec3 all_pts_b[5];
            vec2 all_2d[5];
            float all_depths[5];
            for (uint ai = 0u; ai < 4u; ai++) {
                all_pts_a[ai] = valid_pts_a[ai];
                all_pts_b[ai] = valid_pts_b[ai];
                all_2d[ai] = valid_verts_2d[ai];
                all_depths[ai] = valid_depths[ai];
            }
            all_pts_a[4] = mpr_point_a;
            all_pts_b[4] = mpr_point_b;
            all_2d[4] = mpr_2d;
            all_depths[4] = penetration;

            for (uint ri = 0u; ri < reduced_count; ri++) {
                float best_dist = 1e30;
                uint best_idx = 0u;
                for (uint ci = 0u; ci < 5u; ci++) {
                    float d = length(reduced_2d[ri] - all_2d[ci]);
                    if (d < best_dist) {
                        best_dist = d;
                        best_idx = ci;
                    }
                }
                new_pts_a[ri] = all_pts_a[best_idx];
                new_pts_b[ri] = all_pts_b[best_idx];
                new_depths[ri] = all_depths[best_idx];
            }

            valid_count = reduced_count;
            for (uint ci = 0u; ci < reduced_count; ci++) {
                valid_pts_a[ci] = new_pts_a[ci];
                valid_pts_b[ci] = new_pts_b[ci];
                valid_depths[ci] = new_depths[ci];
            }
        }
    }

    // Write results.
    for (uint k = 0u; k < valid_count && k < 4u; k++) {
        result.contact_points_a[k] = valid_pts_a[k];
        result.contact_points_b[k] = valid_pts_b[k];
        result.penetrations[k] = valid_depths[k];
    }
    result.point_count = valid_count;

    return result;
}

#endif // CONVEX_COLLISION_PERTURBATION_GLSL
