# gpu-convex-collision-detection (delta)

## MODIFIED Requirements

### Requirement: Perturbation-based contact manifold

After MPR finds the base contact normal and point, the shader SHALL expand the contact into a manifold:

1. Compute two orthogonal axes `u, v` perpendicular to the contact normal
2. Generate 6 perturbed directions: for each 60° step around the normal, create a direction tilted 2° from the contact plane using `cos(angle)*sin(2°)*u + sin(angle)*sin(2°)*v + cos(2°)*normal`
3. For each perturbed direction, query `support()` on both shapes, collect the world-space points
4. Fit a contact plane for each shape independently using the incremental largest-triangle method from the collected world-space perturbed points (see `collision-plane-fitting` spec)
5. Project each shape's world-space perturbed points onto the 2D contact plane (u, v axes from the MPR normal), dropping the normal component
6. Apply Sutherland-Hodgman clipping: clip the projected polygon of shape A by the edges of the projected polygon of shape B
7. If the clipped polygon has > 4 vertices, apply the O(N) Rotating Calipers algorithm (see `true-rotating-calipers` spec) to select the 4 vertices forming the maximum-area quadrilateral. The algorithm SHALL find the hull diameter via antipodal walking, then find the two furthest points on opposite sides of the diameter.
8. For each selected 2D vertex, un-project to 3D by ray-casting from the MPR contact plane along the MPR normal onto the fitted plane for that shape, producing independent contact points on shapes A and B
9. Compute per-point penetration depth as `-dot(contact_point_b - contact_point_a, contact_normal)` for each pair (contact_normal points B→A, so the dot product is negative when shapes overlap); discard any point where depth <= -contact_margin (where contact_margin is a configurable uniform), and report `max(depth, 0.0f)` as the penetration for valid points
10. If the dot product of shape A's fitted plane normal and shape B's fitted plane normal is less than `cos(0.1°)`, add the MPR deepest point to the manifold. The MPR point SHALL be unconditionally appended as an additional contact point — it SHALL NOT be subjected to area-based reduction or removal.
11. If no valid contact points remain after validation (step 9) or clipping (step 6), return `point_count == 0` to indicate the MPR fallback should be used

Each valid contact point SHALL produce a separate result entry in the output buffers, with its own `contact_point_a`, `contact_point_b`, and independently computed penetration depth. Up to **5** entries per collision pair SHALL be written (4 from perturbation + optionally 1 MPR fallback).

#### Scenario: Face-to-face contact with tilted surface produces correct contact points

- **WHEN** two boxes rest face-to-face with a slight tilt (< 2°)
- **THEN** the fitted plane for each shape captures the actual surface orientation
- **AND** contact points are projected to the fitted planes (not the theoretical MPR plane)
- **AND** each contact point has its own independently computed depth
- **AND** up to 4 contact points are produced (planes aligned, no MPR fallback needed)

#### Scenario: Edge-to-edge contact produces fewer points

- **WHEN** two boxes contact along edges
- **THEN** the manifold may produce 1-2 contact points
- **AND** the points are valid world-space positions on both shapes' fitted planes

#### Scenario: Non-parallel contacts always retain MPR fallback point

- **WHEN** shape A and shape B fitted plane normals differ by more than 0.1°
- **AND** perturbation already produced 4 valid contact points
- **THEN** the MPR deepest point is appended as a 5th contact point
- **AND** the MPR point is never discarded by area optimization
- **AND** the total manifold contains exactly 5 contact points

#### Scenario: Non-parallel contacts with fewer than 4 perturbation points

- **WHEN** shape A and shape B fitted plane normals differ by more than 0.1°
- **AND** perturbation produced 2 valid contact points
- **THEN** the MPR deepest point is appended as a 3rd contact point
- **AND** no calipers reduction is applied (both counts are ≤ 5)

#### Scenario: Empty manifold falls back to MPR

- **WHEN** perturbation produces zero valid contact points (clipping failure or all points failed penetration check)
- **THEN** `perturb_manifold()` returns `point_count == 0`
- **AND** the caller uses the MPR single contact point as the collision result

### Requirement: Collision result GPU buffers

`ConvexCollisionDetector` SHALL own and expose GPU output buffers for collision results. Each manifold contact point is a separate result entry, so all buffers are sized to `max_collision_pairs * 5` (up to 5 points per collision pair):

- `collision_ids`: `uvec2` buffer storing `(shape_index_a, shape_index_b)` for each contact point
- `collision_normals`: `vec4` buffer storing the contact normal in world space (w = independently computed penetration depth per point)
- `contact_point_a`: `vec4` buffer storing the contact point on shape A in world space
- `contact_point_b`: `vec4` buffer storing the contact point on shape B in world space
- `collision_count`: single `uint` buffer, atomically incremented to reserve slots

All buffers SHALL be separate SSBOs (SoA layout) for cache-friendly access.

#### Scenario: Collision count starts at zero

- **WHEN** a collision detection pass begins
- **THEN** the `collision_count` buffer is reset to 0 before dispatch

#### Scenario: Results written per manifold point, up to 5 per pair

- **WHEN** a compute thread detects a collision and produces N manifold points (1-5)
- **THEN** it atomically adds N to `collision_count` to reserve N contiguous slots
- **AND** writes each point as a separate entry with its own contact positions and independently computed penetration depth
- **AND** if any slot exceeds the buffer capacity, the write is skipped
