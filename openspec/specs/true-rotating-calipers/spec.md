# true-rotating-calipers

## Purpose

Govern the O(N) GPU implementation of the Rotating Calipers algorithm for reducing a convex 2D polygon to at most 4 vertices forming the approximate maximum-area quadrilateral. Replaces the existing brute-force C(N,4) enumeration with the canonical linear-time algorithm.

## Requirements

### Requirement: Rotating Calipers runs in linear time

The `rotating_calipers_reduce` function SHALL run in O(N) time by:
1. **Diameter discovery**: Walking antipodal pairs around the convex hull to find the pair `(p1, p3)` with maximum squared distance (the hull diameter), using the signed-area criterion to advance the antipodal index.
2. **Bilateral extreme point selection**: Scanning all N hull vertices once to find `p2` (maximum signed area on one side of the diameter) and `p4` (maximum signed area on the opposite side).

The algorithm SHALL visit each hull vertex a constant number of times, yielding O(N) total operations regardless of N.

#### Scenario: Six-vertex polygon reduces in ~6 iterations

- **WHEN** the clipped polygon has 6 vertices
- **THEN** the diameter-finding phase advances the antipodal pointer at most N times across the loop
- **AND** the bilateral scan visits each of the 6 vertices exactly once
- **AND** the total operation count is proportional to N, not C(N,4) = 15

#### Scenario: Sixteen-vertex polygon reduces in ~16 iterations

- **WHEN** the clipped polygon has 16 vertices (max possible after Sutherland-Hodgman)
- **THEN** the diameter-finding phase walks the hull in at most 2N advance steps
- **AND** the bilateral scan visits each vertex once
- **AND** the total operation count is ~48 area computations, not C(16,4) = 1820

### Requirement: Diameter discovery via antipodal walking

The diameter-finding phase SHALL:

1. Initialize `p1 = 0`, `p3 = 1`, and `max_dist_sq` from the squared distance between hull[0] and hull[1].
2. Start the antipodal pointer `j = 1`.
3. For each hull vertex `i` from 0 to N-1:
   a. Advance `j` while `signed_area(hull[i], hull[(i+1)%N], hull[(j+1)%N]) > signed_area(hull[i], hull[(i+1)%N], hull[j])`. This advances `j` to the vertex furthest from edge `(i, i+1)`.
   b. Check antipodal pair `(i, j)`: if squared distance exceeds `max_dist_sq * (1 + tie_epsilon_rel)`, update `p1 = i`, `p3 = j`.
   c. Check antipodal pair `(i+1, j)`: if squared distance exceeds `max_dist_sq * (1 + tie_epsilon_rel)`, update `p1 = (i+1)%N`, `p3 = j`.
4. Use relative epsilon `tie_epsilon_rel = 1e-3` for tie-breaking to ensure deterministic results with floating-point near-ties.

The diameter SHALL be represented by the vertex indices `(p1, p3)` that form the maximum-distance antipodal pair discovered.

#### Scenario: Diameter on axis-aligned rectangle

- **WHEN** the polygon is a perfect axis-aligned rectangle with vertices at (0,0), (2,0), (2,1), (0,1)
- **THEN** the diameter is the diagonal between opposite corners
- **AND** the squared distance is 2² + 1² = 5

#### Scenario: Diameter on regular hexagon

- **WHEN** the polygon is a regular hexagon
- **THEN** the diameter connects two opposite vertices
- **AND** the tie-breaking epsilon prevents oscillation between near-equal antipodal pairs

### Requirement: Bilateral extreme point selection

After finding the diameter `(p1, p3)`, the algorithm SHALL find `p2` and `p4` by scanning all N hull vertices:

1. For each vertex `i` from 0 to N-1:
   a. Compute `area = signed_area(hull[p1], hull[p3], hull[i])`.
   b. If `area > max_area_1 * (1 + tie_epsilon_rel)`: update `p2 = i`, `max_area_1 = area`.
   c. If `-area > max_area_2 * (1 + tie_epsilon_rel)`: update `p4 = i`, `max_area_2 = -area`.

This selects the vertices that maximize the quadrilateral area by finding the points furthest from the diameter line on each side.

#### Scenario: Rectangle reduction

- **WHEN** the input polygon is a rectangle with 5+ vertices (e.g., a clipped polygon with collinear edges)
- **THEN** the diameter connects two opposite corners
- **AND** `p2` and `p4` are the vertices on opposite sides of the diameter
- **AND** the resulting quadrilateral is the original rectangle

#### Scenario: Degenerate polygon with all points on one side of diameter

- **WHEN** all N vertices lie on one side of the diameter line (degenerate input)
- **THEN** `max_area_2` remains 0.0
- **AND** `p4` defaults to index 0 (initial value)
- **AND** the function still returns 4 vertex indices without error

### Requirement: Tie-breaking with relative epsilon

All distance and area comparisons SHALL use a scale-invariant relative tie-breaking rule: a new candidate wins only if `new_value > current_best * (1.0 + tie_epsilon_rel)`, where `tie_epsilon_rel = 1.0e-3`.

This SHALL prevent oscillation and non-deterministic selection when multiple antipodal pairs or extreme points have nearly identical distances or areas, which is common for circular or symmetric geometry.

#### Scenario: Nearly circular hull produces stable results

- **WHEN** the input polygon approximates a circle (many vertices at similar distances)
- **THEN** tie-breaking produces the same diameter regardless of vertex enumeration order
- **AND** the output quadrilateral is deterministic frame-to-frame

### Requirement: Output ordering

The result SHALL be a `vec4i` (or equivalent) containing `(p1, p2, p3, p4)` in the order:
- `p1`, `p3`: the diameter endpoints (antipodal pair with maximum distance)
- `p2`: the vertex on one side of the diameter line with maximum signed area
- `p4`: the vertex on the opposite side of the diameter line with maximum signed area

The output vertices SHALL be written in CCW order around the quadrilateral when the input hull is CCW.

#### Scenario: CCW input produces CCW output

- **WHEN** the input polygon is CCW-ordered
- **THEN** the output quad vertices `(p1, p2, p3, p4)` are in CCW order
- **AND** `signed_area(output[0], output[1], output[2])` is positive
