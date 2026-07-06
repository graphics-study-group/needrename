# collision-plane-fitting

## Purpose

Govern the plane-fitting step that replaces the naive uniform-depth un-projection in perturbation manifold generation. Fit a plane from perturbed support points using an incremental largest-triangle tracker, project 2D clipped vertices onto the fitted plane via ray-casting, compute per-point penetration depth with a configurable contact margin, validate actual penetration, and provide fallback mechanisms for degenerate cases.

## Requirements

### Requirement: Plane fitting from perturbed support points

After collecting 6 perturbed world-space support points for shape A and 6 for shape B, the shader SHALL fit a contact plane for each shape independently. The plane SHALL be determined by computing the normal of the largest-area triangle among the perturbed points (the triangle formed by 3 of the 6 collected world-space points that has the maximum cross-product magnitude). The plane SHALL pass through the centroid of all collected perturbed points for that shape.

If fewer than 3 distinct perturbed points are collected (e.g., due to duplicate removal), the shader SHALL attempt to compute a plane normal from the available two points: form a direction vector between them, cross with the MPR contact normal to get a perpendicular vector, then cross back to obtain a plane normal orthogonal to the direction and as close as possible to the MPR normal. Only if this computation also degenerates (the two points are collinear with the MPR normal, or only 1 point exists) SHALL the shader fall back to the MPR contact normal directly.

#### Scenario: Six perturbed points on a slightly tilted plane

- **WHEN** 6 perturbed support points for shape A form a surface tilted ~1.5° from the MPR contact normal
- **THEN** the fitted plane normal is the normal of the largest triangle among the 6 points
- **AND** the fitted plane passes through the centroid of all 6 points
- **AND** the fitted normal is within 2° of the MPR contact normal (since perturbation tilt is 2°)

#### Scenario: Only 2 distinct perturbed points collected

- **WHEN** duplicate removal reduces the perturbed points to only 2 distinct positions
- **THEN** the fitted plane normal is computed from the two available points: `right = cross(p1 - p0, fallback_normal)`, then `normal = normalize(cross(fallback_normal, right))`
- **AND** the plane passes through the midpoint of the 2 points
- **AND** the resulting normal is orthogonal to the line between the two points and as close as possible to the MPR contact normal

#### Scenario: Largest triangle normal is degenerate

- **WHEN** all triangles among the perturbed points have zero area (collinear points)
- **AND** the tracker has at least two distinct points (reference_point ≠ previous_point)
- **THEN** the fitted plane normal is computed from those two points using the 2-point fallback algorithm
- **AND** the plane passes through the centroid of the perturbed points

#### Scenario: Only 1 distinct perturbed point collected

- **WHEN** duplicate removal or degenerate geometry reduces the perturbed points to only 1 distinct position
- **THEN** the fitted plane normal falls back to the MPR contact normal directly
- **AND** the plane passes through that single point

### Requirement: Project 2D clipped vertices onto fitted plane

After 2D Sutherland-Hodgman clipping and rotating calipers reduction produce up to 4 final 2D vertices on the contact plane (u, v axes), each vertex SHALL be un-projected to 3D on the fitted plane, not the theoretical MPR contact plane.

The 2D clipped vertex `(u, v)` SHALL first be converted to a 3D point on the MPR contact plane: `P_contact_plane = contact_center + u*u_axis + v*v_axis`. Then a ray SHALL be cast along the MPR contact normal direction from this point, intersecting it with the fitted plane to produce the final 3D contact position on that shape's fitted surface.

#### Scenario: Vertex projects to tilted fitted plane

- **WHEN** a 2D clipped vertex (0.5, 0.3) on the contact plane is un-projected
- **AND** the fitted plane for shape A has a normal tilted 1.2° from the MPR normal
- **THEN** the 3D contact point on shape A is the intersection of the ray `P_contact_plane + t * MPR_normal` with the fitted plane
- **AND** the contact point position differs from `world_on_plane + contact_normal * half_depth` by a geometry-dependent offset

### Requirement: Per-point penetration depth computation with contact margin

Each contact point pair SHALL have its penetration depth computed independently based on the actual 3D positions on each shape's fitted surface, rather than using a uniform `half_depth = penetration / 2`.

The raw penetration depth for a contact point SHALL be computed as `depth = -dot(contact_point_b - contact_point_a, contact_normal)` (the negation because `contact_normal` points B→A, so `contact_point_b - contact_point_a` points opposite the normal when shapes overlap).

The system SHALL accept a configurable `contact_margin` value (a small positive float, e.g., 0.001). A contact point SHALL be discarded only if `depth <= -contact_margin` (i.e., the separation gap exceeds the margin). Points with `depth > -contact_margin` SHALL be retained — those with positive depth represent actual penetration, and those with negative but small depth represent near-contact speculative contacts.

The reported penetration depth written to the output buffer SHALL be `max(depth, 0.0f)` — speculative contacts (negative raw depth within margin) report zero penetration.

#### Scenario: Two contact points have different penetration depths

- **WHEN** a face-to-face collision produces 2 clipped vertices
- **AND** shape A's fitted plane is tilted 1° relative to shape B's fitted plane
- **THEN** each contact point has its own penetration depth computed independently
- **AND** the two depths may differ by a small amount proportional to the tilt

#### Scenario: Contact point within margin but not penetrating

- **WHEN** a clipped vertex produces contact points with raw depth = -0.0005
- **AND** `contact_margin` is set to 0.001
- **THEN** the contact point is retained (since -0.0005 > -0.001)
- **AND** the reported penetration depth is 0.0 (clamped from negative raw depth)

#### Scenario: Contact point exceeds contact margin

- **WHEN** a clipped vertex produces contact points with raw depth = -0.002
- **AND** `contact_margin` is set to 0.001
- **THEN** the contact point is discarded (since -0.002 <= -0.001)

### Requirement: Penetration validation with configurable contact margin

After computing the 3D contact points on each shape's fitted plane, the shader SHALL validate that each contact point either penetrates or is within the `contact_margin` tolerance. The MPR `contact_normal` points from B toward A. When shapes overlap, `contact_point_a` is shifted in the +normal direction and `contact_point_b` in the -normal direction, so `dot(contact_point_b - contact_point_a, contact_normal)` is negative. Let `depth = -dot(contact_point_b - contact_point_a, contact_normal)`.

A contact point SHALL be considered valid if `depth > -contact_margin`. The reported penetration depth SHALL be `max(depth, 0.0f)`. If `depth <= -contact_margin`, the point is separated beyond the margin and SHALL be discarded.

If a contact point fails validation, it SHALL be silently discarded. The remaining valid contact points SHALL be output. If all contact points are discarded, the perturbation result SHALL have `point_count == 0`, triggering the MPR fallback.

#### Scenario: One of four contact points exceeds margin

- **WHEN** 4 contact points are generated from clipping
- **AND** one of them has raw depth <= -contact_margin (too far separated)
- **THEN** only the 3 valid points are output
- **AND** the result has `point_count == 3`

#### Scenario: All contact points exceed margin

- **WHEN** all clipped vertices, when projected to the fitted planes, have raw depth <= -contact_margin
- **THEN** the perturbation result has `point_count == 0`
- **AND** the caller (detect_collisions.comp) falls back to the MPR single contact point

#### Scenario: Near-contact point within margin is retained

- **WHEN** a clipped vertex produces depth = -0.0003 and contact_margin = 0.001
- **THEN** the contact point is retained as a speculative contact
- **AND** the reported penetration is 0.0

### Requirement: Plane-alignment fallback with MPR deepest point

When the fitted plane for shape A and the fitted plane for shape B have normals whose dot product is less than `cos(0.1°)` (approximately 0.9999985), the two contact surfaces are considered significantly non-parallel. In this case, the MPR deepest point SHALL be unconditionally appended to the contact manifold as an additional contact point alongside any valid perturbation points.

This ensures that when shapes contact at an oblique angle (e.g., edge-on-edge at a sharp angle), at least one contact point is guaranteed to represent the deepest penetration.

The MPR deepest point SHALL be added at most once per collision pair, regardless of how many perturbation points are already present. The MPR point SHALL NOT be subjected to area-based reduction or removal — it is always preserved as a contact point. This allows up to 5 contact points per collision pair (4 from perturbation + 1 MPR fallback).

#### Scenario: Non-parallel contact surfaces add MPR deepest point

- **WHEN** shape A's fitted plane normal and shape B's fitted plane normal have a dot product < cos(0.1°)
- **AND** perturbation produces 2 valid contact points
- **THEN** the MPR contact point (midpoint of deepest penetration) is also added
- **AND** the result has 3 contact points (2 perturbation + 1 MPR)

#### Scenario: Parallel contact surfaces do not add MPR point

- **WHEN** shape A's fitted plane normal and shape B's fitted plane normal have a dot product >= cos(0.1°)
- **AND** perturbation produces 1 or more valid contact points
- **THEN** the MPR deepest point is NOT added
- **AND** the result contains only perturbation contact points

#### Scenario: Non-parallel contacts with 4 perturbation points produce 5 total

- **WHEN** the plane-alignment fallback triggers
- **AND** perturbation already produced 4 valid contact points
- **THEN** the MPR deepest point is unconditionally appended as a 5th contact point
- **AND** no calipers reduction is applied to the combined set
- **AND** the MPR point is never discarded by area optimization

### Requirement: Empty perturbation fallback to MPR

When the perturbation method produces zero valid contact points — either because 2D clipping found no intersection polygon, or because all projected contact points failed penetration validation — the system SHALL fall back to using the MPR single contact point as the collision result.

The MPR fallback point SHALL use the MPR contact point A and contact point B directly, without modification. The fallback SHALL produce exactly 1 contact point entry per collision pair.

#### Scenario: Sutherland-Hodgman clipping produces empty result

- **WHEN** the projected 2D polygons for shapes A and B do not intersect
- **THEN** `ClipResult.vertex_count == 0`
- **AND** the perturbation result has `point_count == 0`
- **AND** detect_collisions.comp falls back to writing the MPR single contact point

#### Scenario: All penetration-validated points are discarded

- **WHEN** Sutherland-Hodgman clipping produces 3 valid 2D vertices
- **AND** all 3 vertices fail penetration validation after projection to fitted planes
- **THEN** the perturbation result has `point_count == 0`
- **AND** detect_collisions.comp falls back to writing the MPR single contact point

### Requirement: Fitted plane computation uses incremental largest-triangle tracking

The shader SHALL determine the contact plane normal using an incremental single-pass tracker as the 6 perturbed points are collected. Only points that survive 2D projection duplicate removal SHALL be fed into the tracker (points whose 2D projection is within `CLIP_EPSILON` of the preceding accepted point or the first accepted point are skipped for plane fitting, but all 6 points contribute to the centroid). The tracker SHALL maintain:

- `reference_point`: the first perturbed point (world-space)
- `previous_point`: the most recently processed point
- `largest_area_sq`: maximum squared cross-product magnitude found so far
- `normal`: the un-normalized cross product of the triangle with the largest area

For each new perturbed point after the second, the shader SHALL form triangle `(reference_point, previous_point, current_point)`, compute `cross(edge1, edge2)` where `edge1 = previous_point - reference_point` and `edge2 = current_point - reference_point`, and update `largest_area_sq` and `normal` if `dot(cross, cross) > largest_area_sq`. The `previous_point` SHALL be updated to `current_point` after each step.

After processing all N points (N between 3 and 6), the fitted plane normal SHALL be `normalize(winning_cross)`. If `largest_area_sq` is zero (all points collinear or fewer than 3 distinct points), the normal SHALL first attempt a 2-point fallback: compute `right = cross(previous_point - reference_point, fallback_normal)` then `normal = normalize(cross(fallback_normal, right))`. Only if this computation also degenerates (the two points are collinear with the fallback normal, or only 1 point exists) SHALL the normal fall back to the MPR contact normal. The plane origin SHALL be the centroid (arithmetic mean) of all N perturbed points.

This method is O(N) and naturally integrates into the perturbation point collection loop without a separate pass.

#### Scenario: All 6 points processed incrementally

- **WHEN** 6 perturbed points are collected, all with distinct 2D projections
- **THEN** the tracker processes 6 points sequentially (all pass duplicate removal)
- **AND** the triangle with the largest `largest_area_sq` determines the fitted plane normal
- **AND** the plane passes through the centroid of all 6 points (including any duplicates, which still contribute to the centroid sum)

#### Scenario: Duplicate 2D projections skipped in tracker

- **WHEN** a perturbed point's 2D projection is within `CLIP_EPSILON` of the previous accepted point or the first accepted point
- **THEN** that point is NOT fed to `update_plane_tracker`
- **AND** the point still contributes to the centroid sum
- **AND** the tracker's `current_point_id` reflects the count of accepted points, not the loop index

#### Scenario: Points are nearly coplanar

- **WHEN** all 6 perturbed points are nearly coplanar (within numerical tolerance)
- **THEN** the incremental tracker still finds a valid normal from the largest triangle
- **AND** the normal is consistent with the actual surface orientation

#### Scenario: All points collinear

- **WHEN** all perturbed points are collinear (forming a line) with at least 2 distinct positions
- **THEN** `largest_area_sq` remains 0.0
- **AND** the 2-point fallback computes a normal from the line direction crossed with the MPR contact normal
- **AND** the fitted plane normal is orthogonal to the collinear line and as close as possible to the MPR normal

#### Scenario: Only 1 distinct point (all duplicates)

- **WHEN** all perturbed points resolve to the same position (e.g., all support queries return the same vertex)
- **THEN** `largest_area_sq` remains 0.0 and `reference_point == previous_point`
- **AND** the 2-point fallback degenerates (direction vector is zero)
- **AND** the fitted plane normal falls back to the MPR contact normal directly

### Requirement: Configurable contact margin uniform

The collision detection compute shader SHALL expose `contact_margin` as a uniform value accessible during penetration validation. The value SHALL flow through the following path:

1. `XpbdConfig` SHALL gain a `float contact_margin` field with a default value of 0.001.
2. `XPBDGpuSolver` SHALL pass `contact_margin` from its config to `ConvexCollisionDetector` during construction.
3. `ConvexCollisionDetector` SHALL accept `float contact_margin` as a constructor parameter and store it.
4. The detector SHALL upload `contact_margin` to a dedicated GPU uniform buffer at shader binding 12 (`DetectorConfig`).
5. `detect_collisions.comp` SHALL declare `layout(set = 0, binding = 12) uniform DetectorConfig { float contact_margin; }` and pass it to `perturb_manifold()`.
6. `perturb_manifold()` SHALL use `contact_margin` in the per-point validation: discard a point only if `depth <= -contact_margin`.

#### Scenario: Contact margin flows from config to shader

- **WHEN** `XpbdConfig::contact_margin` is set to 0.005
- **AND** the XPBD solver constructs the collision detector
- **THEN** the detector uploads 0.005 to the GPU uniform buffer
- **AND** the shader uses 0.005 as the discard threshold in penetration validation

#### Scenario: Default contact margin

- **WHEN** `XpbdConfig` is default-constructed
- **THEN** `contact_margin` is 0.001
- **AND** the collision detector receives 0.001 during construction
