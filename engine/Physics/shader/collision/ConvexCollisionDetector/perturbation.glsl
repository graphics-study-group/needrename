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
//   6. Reduce to at most 4 vertices using O(N) Rotating Calipers
//      (diameter discovery + bilateral extreme point selection).
//   7. Un-project each vertex to 3D by ray-casting from the MPR contact plane
//      along the MPR normal onto each shape's fitted plane.
//   8. Validate each contact point: compute per-point depth; discard points
//      whose separation exceeds contact_margin.
//   9. If the fitted plane normals diverge by more than 0.1°, unconditionally
//      append the MPR deepest point (up to 5 total).
//
// Returns up to 5 contact point pairs on shapes A and B, each with its own
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
    uint current_point_id
) {
    if (current_point_id == 0u) {
        tracker.reference_point = current_point;
        tracker.largest_area_sq = 0.0;
    } else if (current_point_id == 1u) {
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
    vec3 right = cross(tracker.previous_point - tracker.reference_point, fallback_normal);
    vec3 final_normal = cross(fallback_normal, right);
    if (length(final_normal) > CLIP_EPSILON) {
        return normalize(final_normal);
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
    if (abs(denom) < CLIP_EPSILON) {
        return P; // degenerate — ray parallel to plane
    }
    float t = dot(plane_origin - P, plane_normal) / denom;
    return P + t * ray_dir;
}

// ---------------------------------------------------------------------------
// Perturbation manifold result.
// ---------------------------------------------------------------------------

struct PerturbResult {
    vec3 contact_points_a[5];  // world-space on shape A
    vec3 contact_points_b[5];  // world-space on shape B
    float penetrations[5];     // per-point penetration depth
    uint point_count;          // 0-5 (0 = use MPR fallback)
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
//
// Returns up to 5 contact points: 4 from perturbation + optionally 1 MPR
// fallback when fitted planes are non-parallel.
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

        // Accumulate centroid sums.
        centroid_sum_a += world_a;
        centroid_sum_b += world_b;

        // 2D projection for clipping.
        vec3 offset_a = world_a - contact_center;
        vec3 offset_b = world_b - contact_center;

        vec2 projected_a = vec2(dot(offset_a, u), dot(offset_a, v));
        if (poly_a_count == 0u || (length(projected_a - poly_a[poly_a_count - 1u]) > CLIP_EPSILON && length(projected_a - poly_a[0]) > CLIP_EPSILON)) {
            poly_a[poly_a_count] = projected_a;
            update_plane_tracker(tracker_a, world_a, poly_a_count);
            poly_a_count++;
        }
        vec2 projected_b = vec2(dot(offset_b, u), dot(offset_b, v));
        if (poly_b_count == 0u || (length(projected_b - poly_b[poly_b_count - 1u]) > CLIP_EPSILON && length(projected_b - poly_b[0]) > CLIP_EPSILON)) {
            poly_b[poly_b_count] = projected_b;
            update_plane_tracker(tracker_b, world_b, poly_b_count);
            poly_b_count++;
        }
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
    vec3 valid_pts_a[5];
    vec3 valid_pts_b[5];
    float valid_depths[5];
    uint valid_count = 0u;

    for (uint k = 0u; k < final_count && k < 5u; k++) {
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
            valid_depths[valid_count] = max(raw_depth, 0.0);
            valid_count++;
        }
    }

    // Step 9: Plane-alignment fallback — if fitted planes are non-parallel,
    // add the MPR deepest point unconditionally as an additional contact.
    if (dot(fitted_normal_a, fitted_normal_b) < PLANE_ALIGNMENT_EPSILON) {
        if (valid_count < 5u) {
            valid_pts_a[valid_count] = mpr_point_a;
            valid_pts_b[valid_count] = mpr_point_b;
            valid_depths[valid_count] = penetration;
            valid_count++;
        }
    }

    // Write results (up to 5 points: 4 perturbation + optionally 1 MPR fallback).
    for (uint k = 0u; k < valid_count && k < 5u; k++) {
        result.contact_points_a[k] = valid_pts_a[k];
        result.contact_points_b[k] = valid_pts_b[k];
        result.penetrations[k] = valid_depths[k];
    }
    result.point_count = valid_count;

    return result;
}

#endif // CONVEX_COLLISION_PERTURBATION_GLSL
