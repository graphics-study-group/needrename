# gpu-convex-collision-detection

Delta spec for the perturbation-based contact manifold requirement.

## MODIFIED Requirements

### Requirement: Perturbation-based contact manifold

After MPR finds the base contact normal and point, the shader SHALL expand the contact into a manifold:

1. Compute two orthogonal axes `u, v` perpendicular to the contact normal
2. Generate 6 perturbed directions: for each 60° step around the normal, create a direction tilted 2° from the contact plane using `cos(angle)*sin(2°)*u + sin(angle)*sin(2°)*v + cos(2°)*normal`
3. For each perturbed direction, query `support()` on both shapes, collect the world-space points
4. Fit a contact plane for each shape independently using the largest-triangle method from the collected world-space perturbed points (see `collision-plane-fitting` spec)
5. Project each shape's world-space perturbed points onto the 2D contact plane (u, v axes from the MPR normal), dropping the normal component
6. Apply Sutherland-Hodgman clipping: clip the projected polygon of shape A by the edges of the projected polygon of shape B
7. If the clipped polygon has > 4 vertices, apply Rotating Calipers to select the 4 vertices forming the maximum-area quadrilateral
8. For each selected 2D vertex, un-project to 3D by ray-casting from the MPR contact plane along the MPR normal onto the fitted plane for that shape, producing independent contact points on shapes A and B
9. Compute per-point penetration depth as `-dot(contact_point_b - contact_point_a, contact_normal)` for each pair (contact_normal points B→A, so the dot product is negative when shapes overlap); discard any point where depth <= -contact_margin (where contact_margin is a configurable uniform), and report `max(depth, 0.0f)` as the penetration for valid points
10. If the dot product of shape A's fitted plane normal and shape B's fitted plane normal is less than `cos(0.1°)`, add the MPR deepest point to the manifold
11. If the final manifold would exceed 4 points, reduce using rotating calipers on the 2D projected positions
12. If no valid contact points remain after validation (step 9) or clipping (step 6), return `point_count == 0` to indicate the MPR fallback should be used

Each valid contact point SHALL produce a separate result entry in the output buffers, with its own `contact_point_a`, `contact_point_b`, and independently computed penetration depth. Up to 4 entries per collision pair SHALL be written.

#### Scenario: Face-to-face contact with tilted surface produces correct contact points

- **WHEN** two boxes rest face-to-face with a slight tilt (< 2°)
- **THEN** the fitted plane for each shape captures the actual surface orientation
- **AND** contact points are projected to the fitted planes (not the theoretical MPR plane)
- **AND** each contact point has its own independently computed depth
- **AND** up to 4 contact points are produced

#### Scenario: Edge-to-edge contact produces fewer points

- **WHEN** two boxes contact along edges
- **THEN** the manifold may produce 1-2 contact points
- **AND** the points are valid world-space positions on both shapes' fitted planes

#### Scenario: Non-parallel contacts trigger MPR fallback point

- **WHEN** shape A and shape B fitted plane normals differ by more than 0.1°
- **THEN** the MPR deepest point is added to the manifold alongside valid perturbation points
- **AND** if the total would exceed 4, rotating calipers selects the best 4

#### Scenario: Empty manifold falls back to MPR

- **WHEN** perturbation produces zero valid contact points (clipping failure or all points failed penetration check)
- **THEN** `perturb_manifold()` returns `point_count == 0`
- **AND** the caller uses the MPR single contact point as the collision result

## ADDED Requirements

### Requirement: ConvexCollisionDetector accepts contact margin configuration

The `ConvexCollisionDetector` constructor SHALL accept a `float contact_margin` parameter in addition to the existing `RenderSystem &` and `uint32_t max_collision_pairs`. The margin value SHALL be stored and later uploaded to the GPU as a uniform buffer for use in per-point penetration validation.

#### Scenario: Detector constructed with contact margin

- **WHEN** `ConvexCollisionDetector` is constructed with `contact_margin = 0.005`
- **THEN** the margin is stored in the detector's Impl
- **AND** a GPU uniform buffer is allocated/updated with the value 0.005 before shader dispatch

#### Scenario: Zero contact margin is valid

- **WHEN** `contact_margin` is set to 0.0
- **THEN** only points with positive raw penetration (strict overlap) are retained
- **AND** speculative contacts are disabled

### Requirement: Detector config GPU uniform buffer

The `ConvexCollisionDetector` SHALL own a GPU uniform buffer at shader binding 12 containing the `contact_margin` value. The buffer SHALL be a single `float` (4 bytes) and SHALL be updated whenever the margin changes.

The `detect_collisions.comp` shader SHALL declare this buffer as:
```glsl
layout(set = 0, binding = 12) uniform DetectorConfig {
    float contact_margin;
} detector_config;
```

#### Scenario: Uniform buffer created on initialization

- **WHEN** `ConvexCollisionDetector::Step()` is called for the first time
- **THEN** a 4-byte uniform buffer is created for `contact_margin` at binding 12
- **AND** the buffer is bound as a read-only uniform resource in the collision detection compute pass

#### Scenario: Margin updated in buffer before each dispatch

- **WHEN** the collision detection compute pass dispatches
- **THEN** the `contact_margin` value in the GPU buffer matches the value stored in the detector's Impl
