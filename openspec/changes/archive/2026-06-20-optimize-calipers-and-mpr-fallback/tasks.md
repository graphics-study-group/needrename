## 1. GLSL: Replace brute-force calipers with O(N) algorithm

- [x] 1.1 Replace `rotating_calipers_reduce()` body in [clipping.glsl](engine/Physics/shader/solver/ConvexCollisionDetector/clipping.glsl) with the true O(N) Rotating Calipers algorithm: Phase 1 antipodal walking to find diameter `(p1, p3)`, Phase 2 single-pass bilateral scan to find `p2, p4`. Use counted `for` loop for the antipodal advance to avoid unbounded while loop on GPU, with max iterations bounded by `vertex_count`. Add `const float TIE_EPSILON_REL = 1.0e-3` for scale-invariant tie-breaking.
- [x] 1.2 Verify the new calipers function preserves the existing interface: `uint rotating_calipers_reduce(vec2 vertices[MAX_CLIP_VERTS], uint vertex_count, out vec2 result_verts[4])` — output layout unchanged (4 vec2 vertices in CCW order).

## 2. GLSL: Raise PerturbResult capacity from 4 to 5

- [x] 2.1 In [perturbation.glsl](engine/Physics/shader/solver/ConvexCollisionDetector/perturbation.glsl), change all contact-point arrays from size 4 to 5: `PerturbResult.contact_points_a[5]`, `contact_points_b[5]`, `penetrations[5]`. Update the `point_count` comment from `0-4` to `0-5`.
- [x] 2.2 Change validation staging arrays from 4 to 5: `valid_pts_a[5]`, `valid_pts_b[5]`, `valid_verts_2d[5]`, `valid_depths[5]`.
- [x] 2.3 In Step 9 (MPR fallback): remove the `valid_count == 4` branch that merges 5 points and calls `rotating_calipers_reduce` a second time. Replace with a simple `if (valid_count < 5)` guard that appends the MPR deepest point. Remove the nearest-neighbor 2D→3D remapping code block (~60 lines, lines 309-369 in current file).
- [x] 2.4 Update any `k < 4u` loop bounds to `k < 5u` in the final write loop.

## 3. GLSL: Update detect_collisions.comp staging arrays

- [x] 3.1 In [detect_collisions.comp](engine/Physics/shader/solver/ConvexCollisionDetector/detect_collisions.comp), change `pts_a[4]`, `pts_b[4]`, `depths[4]` to `[5]`.
- [x] 3.2 Update the loop bound in the output write section (`for (uint pi = 0u; pi < pert.point_count; pi++)`) to naturally handle up to 5 points (no hardcoded 4 needed if using `pert.point_count` directly).

## 4. C++: Update ConvexCollisionDetector buffer sizing and comments

- [x] 4.1 In [ConvexCollisionDetector.cpp](engine/Physics/Collision/ConvexCollisionDetector.cpp), change the result buffer sizing comment from "up to 4 points per collision pair" to "up to 5 points per collision pair (4 perturbation + optionally 1 MPR fallback)".
- [x] 4.2 In `XPBDGpuSolver.cpp` line 213, change `(shape_count * (shape_count - 1u)) / 2u * 4u` to `* 5u`. Update the comment about max contact points.
- [x] 4.3 In `XPBDGpuSolver.cpp` line 346-350, change the `max_contacts` calculation from `max_pairs * 4u` to `max_pairs * 5u`. Update the comment from "4 manifold points per pair" to "5 manifold points per pair".

## 5. C++: Update header comments

- [x] 5.1 In [ConvexCollisionDetector.h](engine/Physics/Collision/ConvexCollisionDetector.h), update the class-level comment about collision result buffers — change "up to 4 entries per collision pair" to "up to 5 entries per collision pair".

## 6. Build and verify

- [x] 6.1 Build the project with the C++ changes (`cmake --build build`).
- [x] 6.2 Run the physics example and verify visual correctness of collision detection.
- [x] 6.3 Verify that non-parallel plane contacts (edge-on-face, corner-on-face) produce stable contact manifolds without visible jitter or missed contacts.
- [x] 6.4 If GPU debug printf is available, spot-check that the MPR fallback point appears in the output when fitted planes diverge.
