## Why

The current perturbation manifold generation un-projects 2D clipped contact vertices back to 3D using a naive `world_on_plane ± contact_normal * half_depth` formula, which assumes the contact surface is perfectly flat and perpendicular to the MPR contact normal. In practice, the 6 perturbed support points sample real surface geometry that may be tilted (up to 2°), producing contact points that don't lie on a single plane perpendicular to the MPR normal. This produces inaccurate contact point positions with incorrect penetration depths, leading to unstable physics simulation and missed collisions on sloped contacts. Additionally, there is no fallback mechanism when the perturbation method produces degenerate or no results.

## What Changes

- **Plane fitting**: Fit a plane through the 6 perturbed world-space support points (e.g., using the largest triangle's normal) to determine the actual contact surface orientation, replacing the assumption that all points lie on a plane perpendicular to the MPR normal
- **Per-point depth computation**: After 2D clipping, project each vertex onto the fitted plane and independently compute its penetration depth, instead of using a uniform `half_depth` for all points
- **Penetration validation**: Discard contact points that, when mapped back to their original geometry positions, do not actually penetrate the opposing shape
- **Plane-alignment fallback**: When the fitted planes from shape A's and shape B's perturbed points have normals whose dot product is less than `cos(0.1°)` (indicating significantly non-parallel contact surfaces), add the MPR deepest point as an additional contact to ensure at least one valid contact
- **Empty-result fallback**: When perturbation returns no penetrating points (e.g., due to degenerate geometry or numerical issues), fall back to the MPR single contact point
- **Contact margin**: Accept a configurable `contact_margin` value (passed from `XpbdConfig` → `ConvexCollisionDetector` constructor → GPU uniform) that allows contact points with separation distance less than the margin to be retained as speculative contacts, even if they don't strictly penetrate. This enables the XPBD solver to handle near-contact scenarios and prevent tunneling
- **C++ API**: Add `contact_margin` parameter to `ConvexCollisionDetector` constructor; add `contact_margin` field to `XpbdConfig`
- **Modified GLSL functions**: Update `perturbation.glsl` with plane fitting logic, per-point depth computation, penetration validation (with margin), and fallback mechanisms; add `DetectorConfig` uniform buffer to `detect_collisions.comp`

## Capabilities

### New Capabilities

- `collision-plane-fitting`: Fit a best-fit plane from perturbed support points, project 2D clipped vertices onto it, compute per-point penetration depth, and validate that each contact point actually penetrates

### Modified Capabilities

- `gpu-convex-collision-detection`: The perturbation-based contact manifold requirement changes — contact point un-projection no longer uses uniform `half_depth` along the MPR normal; instead uses fitted-plane projection with per-point depth; fallback mechanisms are added for degenerate cases

## Impact

- Affected files: `engine/Physics/shader/solver/ConvexCollisionDetector/perturbation.glsl` (primary), `engine/Physics/shader/solver/ConvexCollisionDetector/detect_collisions.comp` (new uniform buffer + fallback integration)
- C++ API: `ConvexCollisionDetector` constructor gains a `float contact_margin` parameter; `XpbdConfig` gains a `float contact_margin` field; `XPBDGpuSolver::EnsureCollisionDetector()` passes the margin through
- The `PerturbResult` struct and `perturb_manifold()` function signature need to accept the MPR base result for fallback purposes, and the contact margin for validation
- A new GPU uniform buffer (binding 12) exposes `contact_margin` to the collision detection shader
- No breaking changes to the pipeline or build system
