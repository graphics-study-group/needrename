## Context

The current perturbation manifold generation (in `perturbation.glsl`) collects 6 perturbed support points per shape, projects them to 2D, clips the polygons, then un-projects the resulting 2D vertices back to 3D using:
```glsl
vec3 world_on_plane = contact_center + final_verts[k].x * u + final_verts[k].y * v;
result.contact_points_a[k] = world_on_plane + contact_normal * half_depth;
result.contact_points_b[k] = world_on_plane - contact_normal * half_depth;
```

This assumes the contact surface is perfectly planar and perpendicular to the MPR `contact_normal`, which is inaccurate when the perturbed points sample geometry with slight slopes (up to the 2° perturbation tilt). The uniform `half_depth` also ignores that different contact points may have different penetration depths.

Additionally, there is no fallback when the perturbation method produces degenerate results — the only existing fallback is when `pert.point_count == 0`, which triggers MPR fallback in `detect_collisions.comp`. But there is no detection of near-degenerate cases where the two contact surfaces are non-parallel enough that perturbation-only contacts could be unreliable.

## Goals / Non-Goals

**Goals:**
- Fit a plane through the perturbed world-space support points to determine the actual contact surface orientation
- Compute per-point penetration depth instead of using uniform `half_depth`
- Validate that each contact point actually penetrates (or is within `contact_margin`), discarding invalid ones
- Add a configurable `contact_margin` tolerance that allows near-contact points (separation < margin) to be retained as speculative contacts
- Add fallback when fitted plane normals diverge by more than 0.1° (add MPR deepest point)
- Preserve existing fallback when perturbation returns zero points (use MPR single point)

**Non-Goals:**
- Changing the 2D clipping algorithm (Sutherland-Hodgman) or rotating calipers reduction
- Changing the perturbation direction computation (still 6 directions at 60° steps, 2° tilt)
- Changing the MPR algorithm itself
- Changing the output buffer format
- Supporting shapes other than boxes for now (the support function dispatches by shape type)

## Decisions

### Decision 1: Incremental plane fitting

**Choice**: Fit the contact plane using an incremental single-pass tracker as the 6 perturbed points are collected. Maintain a `reference_point` (first point), `previous_point` (last point processed), `largest_area_sq` (maximum cross-product squared magnitude found), and `normal` (un-normalized cross product of the winning triangle). For each new point after the second, form triangle `(reference_point, previous_point, current_point)`, compute `|cross(edge1, edge2)|²`, and keep the normal with the largest area.

After processing all N points, the fitted plane normal is `normalize(winning_cross)`. The plane origin is the centroid of all N points.

**Alternatives considered**:
- *Brute-force C(N,3)*: O(N³) with N ≤ 6 evaluates all 20 triangles. Works but does redundant work — many triangles share edges. The incremental approach is O(N) and naturally handles varying point counts.
- *Newell's method (area-weighted normal sum)*: Area-weighted average of edge cross products, O(N). Good for polygon normals but can be skewed if one edge dominates. The incremental max-area approach picks the dominant planar feature instead.
- *PCA / SVD*: Too expensive for a GPU shader.

**Rationale**: The incremental approach naturally extends the perturbation point collection loop: each new support point immediately updates the plane tracker without a second pass. O(N) with N ≤ 6 is trivial but cleaner than enumerating all 20 triangles. Picking the max-area triangle resists outliers — the dominant planar region on the contact surface wins. The centroid offset ensures the plane is well-positioned even if the winning triangle is off-center.

### Decision 2: Ray-cast un-projection

**Choice**: Un-project a 2D clipped vertex to 3D by starting at the MPR contact plane position and ray-casting along the MPR normal until intersecting the fitted plane.

**Alternatives considered**:
- *Direct projection onto fitted plane*: Project the contact-plane point orthogonally onto the fitted plane. This is directionally wrong — the contact plane's u,v axes are perpendicular to the MPR normal, not the fitted plane normal. Orthogonal projection would shift the point in u,v space.
- *Interpolation from perturbed points*: Use barycentric coordinates from the 2D clipping result to interpolate among the original perturbed 3D points. This preserves the sampled geometry exactly but is fragile when the clipping result lies outside the convex hull of perturbed points (which can happen with Sutherland-Hodgman).

**Rationale**: Ray-casting along the MPR normal is the correct inverse of the 2D projection step. In step 5, we project perturbed 3D points onto the contact plane by dropping the normal component. The inverse is to start at the contact-plane position and move along the normal until hitting the actual geometry surface (the fitted plane). This preserves the u,v coordinates exactly.

### Decision 3: Penetration validation with contact margin

**Choice**: A contact point is valid if `dot(contact_point_b - contact_point_a, contact_normal) < contact_margin`.

Where `contact_margin` is a small positive configurable value (e.g., 0.001 world units). Equivalently, define `depth = -dot(contact_point_b - contact_point_a, contact_normal)` and keep the point if `depth > -contact_margin`.

**Penetration depth reported**: `max(depth, 0.0f)` — negative depths (non-penetrating but within margin) are clamped to zero, since the XPBD solver already handles zero-penetration contacts correctly via its constraint formulation.

**Rationale**: The MPR `contact_normal` points from B toward A. The dot product sign convention is:
```
When shapes overlap:   dot(contact_point_b - contact_point_a, contact_normal) < 0  → depth > 0
When shapes separated:  dot(contact_point_b - contact_point_a, contact_normal) > 0  → depth < 0
When exactly touching:  dot(...) = 0  → depth = 0
```
Without margin, we discard when `depth <= 0`. With margin `m`, we keep points where `depth > -m` — i.e., the gap is less than `m`. This allows the XPBD solver to handle near-contact scenarios and helps prevent tunneling. Points closer than `contact_margin` but not penetrating are reported as speculative contacts with depth clamped to 0.

**Why not always keep near-contact points?** The margin is configurable because different simulations have different scale requirements. A physics scene with meter-scale objects needs a different margin than centimeter-scale objects. Passing it from `XpbdConfig` through the detector constructor lets the application tune it.

### Decision 4: cos(0.1°) threshold for plane-alignment fallback

**Choice**: When `dot(normal_A_fitted, normal_B_fitted) < cos(0.1°) ≈ 0.9999985`, add the MPR deepest point.

**Rationale**: The perturbation tilt is 2°, so realistically the fitted normals should be within ~2° of the MPR normal and within ~4° of each other. A 0.1° threshold is conservative — it only triggers when the surfaces are very clearly non-parallel. At this degree of non-parallelism, the perturbation manifold geometry may not reliably capture the deepest penetration, so the MPR point provides a guaranteed valid contact at the cost of one extra point.

### Decision 5: Modify PerturbResult to accept MPR data for fallback

**Choice**: Pass the MPR `base_result` (or at minimum the MPR contact points) and `contact_margin` into `perturb_manifold()` so the function can add the MPR deepest point to the result when the plane-alignment fallback triggers, and apply the margin during penetration validation.

**Alternatives considered**:
- *Handle fallback entirely in detect_collisions.comp*: The caller checks the condition and adds the MPR point. This keeps perturbation.glsl simpler but duplicates fallback logic and requires the caller to understand fitted plane internals.
- *Return fitted plane info to caller*: Adds complexity to the return struct.

**Rationale**: Having `perturb_manifold()` handle the fallback internally keeps the fallback logic co-located with the plane fitting code. The function already knows the fitted plane normals; exposing this to the caller would leak implementation details. The additional parameters are minimal (two `vec3` values for the MPR contact points, one `float` for the margin).

### Decision 6: Contact margin data flow

**Choice**: The `contact_margin` value flows through:
1. `XpbdConfig` gains a `float contact_margin` field (default ~0.001)
2. `ConvexCollisionDetector` constructor gains a `float contact_margin` parameter
3. The detector stores it and uploads it to a GPU uniform buffer (`DetectorConfig`, binding 12, containing a single `float`)
4. `detect_collisions.comp` declares `layout(set = 0, binding = 12) uniform DetectorConfig { float contact_margin; }` and passes it to `perturb_manifold()`
5. `perturb_manifold()` uses it in the validation: discard if `depth <= -contact_margin`

**Alternatives considered**:
- *Push constant*: Simpler API, but push constants have limited size and aren't shared across shader stages well. A uniform buffer is more consistent with the existing SSBO pattern.
- *Specialization constant*: No runtime changes possible without recompiling the pipeline. The margin should be tunable per-scene.
- *Hardcoded constant*: No flexibility across different simulation scales.

**Rationale**: The uniform buffer approach matches the existing `ShapeSlotCount` pattern (binding 11). It's a small buffer (4 bytes) that's uploaded once on config change and read by every shader invocation. This is consistent with how `XpbdConfig` already drives solver parameters — the margin is just another config field.

## Risks / Trade-offs

- **[Risk] Incremental largest-triangle normal may be noisy with very small penetration** → **Mitigation**: The tracker already maximizes `largest_area_sq`, which penalizes small/degenerate triangles. If all triangles are degenerate (e.g., all points collinear, `largest_area_sq` stays 0), fall back to MPR normal.
- **[Risk] Ray-casting against fitted plane may produce a point far from the actual geometry if the fitted plane extrapolates poorly** → **Mitigation**: The fitted plane is determined by 6 support points that bound the contact region. Extrapolation distance is limited because the 2D clipping polygon is contained within the convex hull of projected perturbed points, so the ray origin is always near the fitted plane's defining points.
- **[Risk] cos(0.1°) threshold may be too sensitive, causing unnecessary MPR point injection** → **Mitigation**: The MPR point is just one extra contact — at worst, it adds a contact point that's already close to the perturbation points. The rotating calipers pass will select the best 4-point representation regardless.
- **[Risk] Per-point depth validation may discard too many points, reducing manifold quality** → **Mitigation**: If ALL points are discarded, the function returns `point_count == 0`, and the existing MPR fallback path in `detect_collisions.comp` ensures at least one contact point.
- **[Risk] Contact margin too large may generate spurious contacts on nearby but non-interacting shapes** → **Mitigation**: The default margin is small (~0.001 world units). The value is configurable in `XpbdConfig` so the application can tune it per-scene. Spurious contacts with zero penetration are harmless for XPBD — the solver only applies forces when the constraint is violated.

## Open Questions

- Should the `cos(0.1°)` threshold be tunable or a named constant? Decided: named constant `PLANE_ALIGNMENT_EPSILON` in perturbation.glsl, matching the existing pattern of `MPR_EPSILON` and `CLIP_EPSILON`.
- Default value for `contact_margin`? Decided: `0.001f` (1 mm in meter-scale scenes), configurable via `XpbdConfig::contact_margin`.
