## Why

The current perturbation contact manifold pipeline has three interconnected problems: (1) `rotating_calipers_reduce` is a brute-force O(N⁴) combinatorial search masquerading as the true O(N) Rotating Calipers algorithm; (2) the calipers reduction is called twice — once after Sutherland-Hodgman clipping and again when the MPR fallback fires on non-parallel fitted planes — with a fragile nearest-neighbor 3D-remapping step in between; (3) the MPR fallback point, intended as a safety net for divergent contact surfaces, can be silently discarded by the second calipers pass because calipers optimizes for maximum quadrilateral area rather than preserving the fallback. Additionally, raising the per-pair contact cap from 4 to 5 eliminates the need for the second calipers call entirely, simplifies the pipeline, and improves contact coverage for non-parallel contacts.

## What Changes

- **Replace brute-force quadrilateral selection** in `rotating_calipers_reduce` with the true O(N) Rotating Calipers algorithm as implemented in `reference_clipping.py`: first find the convex hull diameter via antipodal walking, then find the two furthest points on either side of the diameter. This preserves the O(N) guarantee and aligns with the function's name.
- **Eliminate the second calipers call** in Step 9 of `perturb_manifold()` (the plane-alignment fallback path). When the MPR fallback fires and we already have 4 perturbation points, the MPR point is now unconditionally appended as a 5th point without reduction.
- **Increase max contact points per detection pair from 4 to 5** (`PerturbResult` arrays, `detect_collisions.comp` output staging, GPU buffer sizing, XPBD solver dispatch sizing, and all relevant comments/constants).
- **Guarantee MPR fallback preservation**: the MPR deepest point is always included in the final manifold when the fitted planes diverge by >0.1°, never subject to area-optimization trimming.

## Capabilities

### New Capabilities

- `true-rotating-calipers`: An O(N) reduction from a convex 2D polygon to at most 4 vertices forming the approximate maximum-area quadrilateral, using the canonical Rotating Calipers algorithm (diameter discovery + bilateral extreme point selection) instead of brute-force C(N,4) enumeration.

### Modified Capabilities

- `gpu-convex-collision-detection`: Manifold perturbation step 11 (MPR fallback reduction) changes from "reduce combined set of 5 back to 4 via rotating calipers" to "unconditionally retain the MPR fallback point, allowing up to 5 contact points per pair." Step 10 changes from "reduce using rotating calipers on the 2D projected positions" to "reduce to at most 4 using the true O(N) rotating calipers algorithm." The per-pair output cap changes from 4 to 5 contact points.
- `xpbd-contact-solve`: Contact count guard dispatch sizing changes from `max_pairs * 4` to `max_pairs * 5`. The per-constraint accumulation pass and per-body application pass already handle an arbitrary flat contact list, so no shader logic changes — only buffer sizing and dispatch workgroup counts.

## Impact

- **GLSL shaders**: `clipping.glsl` (replace function body), `perturbation.glsl` (arrays 4→5, simplify Step 9), `detect_collisions.comp` (staging arrays 4→5)
- **C++ detector**: `ConvexCollisionDetector.cpp` (buffer sizing `* 4u` → `* 5u`, comment updates)
- **C++ solver**: `XPBDGpuSolver.cpp` (`max_contacts` calculation `* 4u` → `* 5u` in two locations, comment updates)
- **Specs**: delta specs for `gpu-convex-collision-detection` (steps 7, 10–11, output count) and `xpbd-contact-solve` (contact count guard, buffer sizing)
- **No breaking changes** to public API — `ConvexCollisionDetector` and `XPBDGpuSolver` constructors and public methods are unchanged
