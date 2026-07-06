// mpr.glsl — Minkowski Portal Refinement for convex collision detection.
//
// Implements the MPR algorithm that operates on the Minkowski difference (CSO)
// of two convex shapes.  Uses only support() queries, making it compatible
// with any convex shape that provides a support function.
//
// Reference: "XenoCollide" by Gary Snethen / Game Physics Pearls.

#ifndef CONVEX_COLLISION_MPR_GLSL
#define CONVEX_COLLISION_MPR_GLSL

const float MPR_EPSILON = 1e-6;
const int MPR_MAX_ITERATIONS = 32;

// ---------------------------------------------------------------------------
// CSO vertex — a point on the Minkowski difference surface with its
// decomposition into the two shape support points that generated it.
// ---------------------------------------------------------------------------

struct CsoVertex {
    vec3 point_cso;  // = point_a - point_b, on the CSO surface
    vec3 point_a;    // = support(shape_a, dir), world-space point on A
    vec3 point_b;    // = support(shape_b, -dir), world-space point on B
};

CsoVertex cso_support(uint shape_a, uint shape_b, vec3 dir) {
    CsoVertex v;
    v.point_a = support(shape_a, dir);
    v.point_b = support(shape_b, -dir);
    v.point_cso = v.point_a - v.point_b;
    return v;
}

// ---------------------------------------------------------------------------
// MPR result
// ---------------------------------------------------------------------------

struct MprResult {
    bool hit;              // true if the shapes overlap
    vec3 contact_normal;   // world-space normal pointing from B toward A
    float penetration;     // penetration depth (positive when overlapping)
    vec3 contact_point_a;  // contact point on A (world space)
    vec3 contact_point_b;  // contact point on B (world space)
};

// ---------------------------------------------------------------------------
// Compute a point inside the CSO.
// ---------------------------------------------------------------------------

vec3 interior_point(uint shape_a, uint shape_b) {
    // Use the difference of shape world positions as an interior point.
    // For separated objects this is outside the CSO, but the MPR refinement
    // will detect that.
    vec3 center_a = shape_world_position.v[shape_a].xyz;
    vec3 center_b = shape_world_position.v[shape_b].xyz;
    return center_a - center_b;
}

// ---------------------------------------------------------------------------
// MPR detection
// ---------------------------------------------------------------------------

MprResult mpr_detect(uint shape_a, uint shape_b) {
    MprResult result;
    result.hit = false;
    result.contact_normal = vec3(0.0, 0.0, 1.0);
    result.penetration = 0.0;
    result.contact_point_a = vec3(0.0);
    result.contact_point_b = vec3(0.0);

    // ---- Phase 1: Portal Discovery -----------------------------------------
    //
    // Build a tetrahedron (V0, V1, V2, V3) where:
    //   V0 = interior point of the CSO (never modified)
    //   V1, V2, V3 = portal triangle on the CSO surface
    //
    // The origin ray goes from V0 toward the origin (0,0,0).  The portal
    // triangle must be positioned such that this ray passes through it.

    // Step 1: Pick an interior point V0.
    vec3 V0 = interior_point(shape_a, shape_b);
    float V0_len = length(V0);
    if (V0_len < MPR_EPSILON) {
        // Coincident centers — offset slightly.
        V0 = vec3(MPR_EPSILON, 0.0, 0.0);
        V0_len = MPR_EPSILON;
    }

    // Step 2: Find V1 — first portal point along the ray from V0 to origin.
    vec3 origin_dir = normalize(-V0);
    CsoVertex V1 = cso_support(shape_a, shape_b, origin_dir);

    // If V1 is behind the origin (relative to V0), there is no overlap.
    if (dot(V1.point_cso, -V0) <= MPR_EPSILON) {
        return result;
    }

    // Step 3: Find V2 — second portal point, perpendicular to plane (V0, origin, V1).
    vec3 n_plane = cross(-V0, V1.point_cso); // normal of plane through V0, origin, V1
    float n_plane_len = length(n_plane);
    if (n_plane_len < MPR_EPSILON) {
        // V1 lies exactly on the origin ray — simple single-point contact.
        float dist = length(V1.point_cso);
        if (dist > MPR_EPSILON) {
            result.hit = true;
            result.penetration = dist;
            result.contact_normal = normalize(-V0);
            result.contact_point_a = V1.point_a;
            result.contact_point_b = V1.point_b;
        }
        return result;
    }
    vec3 dir_V2 = n_plane / n_plane_len;
    CsoVertex V2 = cso_support(shape_a, shape_b, dir_V2);

    if (dot(V2.point_cso, dir_V2) <= MPR_EPSILON) {
        return result; // CSO does not extend far enough — no collision.
    }

    // Step 4: Find V3 — completes the tetrahedron.
    vec3 n3 = cross(V1.point_cso - V0, V2.point_cso - V0);
    // Make n3 point away from V0 (toward the origin side).
    if (dot(V0, n3) > 0.0) {
        n3 = -n3;
    }
    float n3_len = length(n3);
    if (n3_len < MPR_EPSILON) {
        return result; // degenerate V0-V1-V2 plane.
    }
    CsoVertex V3 = cso_support(shape_a, shape_b, n3 / n3_len);

    // Step 5: Validate portal — the origin ray from V0 must intersect the
    // portal triangle V1-V2-V3.  If not, replace a vertex and retry.
    const int VALIDATE_MAX = 6;
    for (int vi = 0; vi < VALIDATE_MAX; vi++) {
        // Test face V0-V1-V3: does it separate V2 from the origin?
        vec3 f13 = cross(V1.point_cso - V0, V3.point_cso - V0);
        if (dot(f13, V2.point_cso - V0) * dot(f13, -V0) < 0.0) {
            // V2 and origin are on opposite sides — replace V2.
            V2 = V3;
            n3 = cross(V1.point_cso - V0, V2.point_cso - V0);
            if (dot(V0, n3) > 0.0) { n3 = -n3; }
            V3 = cso_support(shape_a, shape_b, normalize(n3));
            continue;
        }
        // Test face V0-V2-V3: does it separate V1 from the origin?
        vec3 f23 = cross(V2.point_cso - V0, V3.point_cso - V0);
        if (dot(f23, V1.point_cso - V0) * dot(f23, -V0) < 0.0) {
            // V1 and origin are on opposite sides — replace V1.
            V1 = V3;
            n3 = cross(V1.point_cso - V0, V2.point_cso - V0);
            if (dot(V0, n3) > 0.0) { n3 = -n3; }
            V3 = cso_support(shape_a, shape_b, normalize(n3));
            continue;
        }
        // Both tests passed — portal is valid.
        break;
    }

    // ---- Phase 2: Portal Refinement ----------------------------------------
    //
    // Iteratively expand the portal face outward (toward the CSO boundary
    // in the direction of the portal normal) until it can no longer be
    // expanded.  At that point the portal IS the CSO face closest to the
    // origin — if the origin lies behind it, the shapes overlap.
    //
    // Key insight: even when the origin is inside the portal plane, we MUST
    // keep expanding — the current portal may be an inner approximation of
    // the true CSO face.

    for (int iter = 0; iter < MPR_MAX_ITERATIONS; iter++) {
        // Compute portal normal — points away from V0 (toward origin).
        vec3 portal_normal = cross(V2.point_cso - V1.point_cso, V3.point_cso - V1.point_cso);
        float pn_len = length(portal_normal);
        if (pn_len < MPR_EPSILON) {
            return result; // degenerate portal.
        }
        portal_normal = portal_normal / pn_len;

        // Ensure portal normal points away from V0.
        if (dot(portal_normal, V1.point_cso - V0) < 0.0) {
            portal_normal = -portal_normal;
        }

        // Signed distance from origin to the portal plane.
        float dist_to_origin = dot(portal_normal, V1.point_cso);

        // ---- Get V4: the farthest point in the portal normal direction. ----
        CsoVertex V4 = cso_support(shape_a, shape_b, portal_normal);
        float dot_V4 = dot(V4.point_cso, portal_normal);

        // ---- Miss check: origin past the farthest CSO extent. ----
        if (dot_V4 < -MPR_EPSILON) {
            return result; // origin is outside the CSO — no collision.
        }

        // ---- Convergence / hit check ----
        //
        // The portal is expanding in the normal direction.  When the new
        // support point V4 does not extend significantly beyond the portal
        // plane, the portal IS the CSO face — further expansion is
        // impossible.  We are done.
        //
        // Use the portal vertex with the largest projection for the delta
        // check (robust against replacing a vertex).
        float portal_proj = max(
            max(dot(V1.point_cso, portal_normal), dot(V2.point_cso, portal_normal)),
            dot(V3.point_cso, portal_normal)
        );
        float delta = dot_V4 - portal_proj;

        if (delta < MPR_EPSILON) {
            // Portal cannot expand further — the current portal face IS a
            // face of the CSO.
            if (dist_to_origin > MPR_EPSILON) {
                // Origin is behind the CSO face → overlap!
                result.hit = true;
                result.penetration = dist_to_origin;
                result.contact_normal = portal_normal;

                // ---- Barycentric contact point extraction ----
                vec3 proj_cso = dist_to_origin * portal_normal;
                vec3 a = V1.point_cso;
                vec3 b = V2.point_cso;
                vec3 c = V3.point_cso;
                float area_total = length(cross(b - a, c - a));
                float u, v, w;
                if (area_total < MPR_EPSILON) {
                    u = 1.0 / 3.0; v = 1.0 / 3.0; w = 1.0 / 3.0;
                } else {
                    u = length(cross(b - proj_cso, c - proj_cso)) / area_total;
                    v = length(cross(c - proj_cso, a - proj_cso)) / area_total;
                    w = length(cross(a - proj_cso, b - proj_cso)) / area_total;
                    float sum_uvw = u + v + w;
                    u /= sum_uvw; v /= sum_uvw; w /= sum_uvw;
                }
                result.contact_point_a = u * V1.point_a + v * V2.point_a + w * V3.point_a;
                result.contact_point_b = u * V1.point_b + v * V2.point_b + w * V3.point_b;
            }
            // (If dist <= 0, origin is outside the CSO — touching or separated.)
            return result;
        }

        // ---- Portal expansion: replace one vertex with V4. ----
        //
        // Signed volumes of tetrahedra (V4, V0, Vi) tell us which portal
        // face the origin ray passes through, i.e. which vertex to replace.

        float d1 = dot(cross(V4.point_cso, V1.point_cso), V0);
        float d2 = dot(cross(V4.point_cso, V2.point_cso), V0);
        float d3 = dot(cross(V4.point_cso, V3.point_cso), V0);

        if (d1 < 0.0) {
            if (d2 < 0.0) {
                V1 = V4;
            } else {
                V3 = V4;
            }
        } else {
            if (d3 < 0.0) {
                V2 = V4;
            } else {
                V1 = V4;
            }
        }
    }

    // Max iterations exceeded.
    return result;
}

#endif // CONVEX_COLLISION_MPR_GLSL
