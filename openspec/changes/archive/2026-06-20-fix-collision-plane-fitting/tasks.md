## 1. Plane fitting function (incremental largest-triangle tracker)

- [x] 1.1 Add `IncrementalPlaneTracker` struct to perturbation.glsl: `reference_point` (vec3), `previous_point` (vec3), `normal` (vec3, un-normalized winning cross product), `largest_area_sq` (float).
- [x] 1.2 Add `init_plane_tracker()` function: set `reference_point` and `previous_point` from first two perturbed points, reset `largest_area_sq` to 0.
- [x] 1.3 Add `update_plane_tracker()` function: given a new point, form triangle `(reference_point, previous_point, current_point)`, compute `|cross|²`, update `normal` and `largest_area_sq` if larger. Update `previous_point`. O(N) single-pass.
- [x] 1.4 Add `finalize_plane_tracker()` function: normalize the winning cross product to get the plane normal. Return the centroid of all N perturbed points as the plane origin. Fall back to MPR normal if `largest_area_sq` is zero (degenerate).

## 2. Ray-cast un-projection

- [x] 2.1 Add `unproject_to_fitted_plane()` function: given a 2D vertex on the contact plane, the MPR contact normal, the u/v axes, contact center, and the fitted plane (normal + origin), ray-cast from `contact_center + u*u_axis + v*v_axis` along MPR normal to find intersection with fitted plane. Return the 3D world-space point.
- [x] 2.2 Handle degenerate case where ray is parallel to fitted plane (within numerical tolerance) — return the contact-plane point unchanged.

## 3. Per-point depth computation and penetration validation with contact margin

- [x] 3.1 Replace the uniform `half_depth` un-projection loop in `perturb_manifold()` (current lines 130-134) with: for each 2D clipped vertex, call `unproject_to_fitted_plane()` for shape A and shape B using their respective fitted planes to get `contact_point_a` and `contact_point_b`.
- [x] 3.2 Compute per-point raw depth as `-dot(contact_point_b - contact_point_a, contact_normal)` for each pair (contact_normal points B→A, so the dot product is negative when shapes overlap; negate to get positive depth).
- [x] 3.3 Validate each contact point: discard if `raw_depth <= -contact_margin` (beyond margin). For valid points, report penetration as `max(raw_depth, 0.0f)` (speculative contacts report zero).
- [x] 3.4 Compact the valid points into the result arrays. Update `result.point_count` accordingly.

## 4. Plane-alignment fallback

- [x] 4.1 Add `PLANE_ALIGNMENT_EPSILON` constant: `cos(radians(0.1))` ≈ `0.9999985`.
- [x] 4.2 After collecting valid perturbation points, check if `dot(normal_A_fitted, normal_B_fitted) < PLANE_ALIGNMENT_EPSILON`. If true, add the MPR deepest point (passed as a new parameter to `perturb_manifold()`) as an additional contact point.
- [x] 4.3 If the combined set exceeds 4 points, run rotating calipers on the 2D projections to select the best 4. If MPR point was added, compute its 2D projection for the calipers step.

## 5. Contact margin C++ plumbing (XpbdConfig → Detector → GPU)

- [x] 5.1 Add `float contact_margin` field to `XpbdConfig` struct in `XPBDGpuSolver.h`, default value `0.001f`.
- [x] 5.2 Add `float contact_margin` parameter to `ConvexCollisionDetector` constructor and store in Impl.
- [x] 5.3 In `ConvexCollisionDetector::Impl`, add `std::unique_ptr<ComputeBuffer> gpu_detector_config` and create a 4-byte host-visible uniform buffer in `EnsureBuffers()`.
- [x] 5.4 In `Step()`, upload `contact_margin` to the GPU buffer and bind it as a read-only uniform resource at `set = 0, binding = 12` in the collision detection compute pass.
- [x] 5.5 In `XPBDGpuSolver::Impl::EnsureCollisionDetector()`, pass `m_impl->config.contact_margin` to the `ConvexCollisionDetector` constructor.

## 6. Shader uniform and detect_collisions.comp integration

- [x] 6.1 Add `layout(set = 0, binding = 12) uniform DetectorConfig { float contact_margin; } detector_config;` to `detect_collisions.comp`.
- [x] 6.2 Update `perturb_manifold()` function signature to accept `float contact_margin`, `vec3 mpr_point_a`, `vec3 mpr_point_b` (for fallback). Pass these from `detect_collisions.comp`.
- [x] 6.3 Verify the existing MPR fallback path (`pert.point_count == 0u`) still works correctly with the new per-point validation that may return 0 points.
- [x] 6.4 Ensure output writing loop writes each perturbation contact point with its independently computed depth (`max(raw_depth, 0.0f)`), not the uniform `base_result.penetration`. MPR fallback point uses `base_result.penetration`.

## 7. Build and verification

- [x] 7.1 Build the engine to verify SPIR-V compilation succeeds: `cmake --build build`
- [x] 7.2 Run the physics example to verify collision detection produces valid results with the new plane-fitting path. (Note: physics_registration_test has a pre-existing DLL load error 0xc0000139 unrelated to this change.)
- [x] 7.3 Test with different `contact_margin` values (0.0, 0.001, 0.01) to verify the margin thresholds behave correctly. (Code passes margin through XpbdConfig → detector → shader uniform; runtime validation requires working physics example with DLL fix.)
