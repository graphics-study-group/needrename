## 1. GLSL Shader Infrastructure — Support Function + MPR

- [x] 1.1 Create shader directory `engine/Physics/shader/solver/ConvexCollisionDetector/`
- [x] 1.2 Write `support.glsl`: define `support()` function signature, implement `support_box()` with world↔local transform, write dispatch stub that switches on `shape_type`
- [x] 1.3 Write `mpr.glsl`: implement MPR algorithm — interior point discovery, portal refinement loop (max 32 iterations), penetration depth and contact normal extraction, using only `support()` queries
- [x] 1.4 Write `generate_pairs.comp`: GPU-side all-pairs pair generation — dispatch `max_pairs` threads, each thread computes upper-triangle pair `(i, j)` from `gl_GlobalInvocationID.x` using row-accumulation formula, writes `uvec2(i, j)` to output SSBO; threads beyond N*(N-1)/2 early-return
- [x] 1.5 Write `detect_collisions.comp`: main compute shader entry point — bind PhysicsScene SSBOs + collision pair input buffer (from generate_pairs) + result output buffers, call MPR per pair, write results via atomic counter, add `debugPrintfEXT` output
- [x] 1.6 Verify CMake GLSL→SPIR-V compilation: build `physics_shader` target, confirm both `generate_pairs.comp.spv` AND `detect_collisions.comp.spv` are produced, fix any GLSL compilation errors
- [x] 1.7 If glslangValidator `#include` is problematic, adjust CMake to add `-I` flags per shader directory or implement a simple concatenation preprocess step

## 2. C++ ConvexCollisionDetector Class

- [x] 2.1 Create `engine/Physics/Collision/` directory and `ConvexCollisionDetector.h` — class declaration with constructor(`RenderSystem&`, `max_collision_pairs`), `Step()` method, buffer getters, PIMPL pattern
- [x] 2.2 Create `ConvexCollisionDetector.cpp` — implement SPIR-V loading for both `generate_pairs.comp.spv` AND `detect_collisions.comp.spv`, create two `ComputeStage` instances + `ComputeResourceBinding` for each
- [x] 2.3 Create collision pair GPU buffer: `m_gpu_collision_pairs` (uvec2, size `max_collision_pairs`), owned by the detector
- [x] 2.4 Create result GPU buffers: `collision_ids` (uvec2), `collision_normals` (vec4), `contact_point_a` (vec4), `contact_point_b` (vec4), `collision_count` (uint) — all using `ComputeBuffer::CreateUnique` with capacity `max_collision_pairs`
- [x] 2.5 Implement `Step(builder, physics_scene)`
- [x] 2.6 Implement buffer reset: collision_count reset by generate_pairs.comp thread 0
- [x] 2.7 Expose result buffer pointers via getters for downstream consumption
- [x] 2.8 Add the new `.cpp` file to `engine/Physics/CMakeLists.txt` (the GLOB_RECURSE picks it up automatically)

## 3. Contact Manifold — Perturbation + Clipping

- [x] 3.1 Write `perturbation.glsl`: compute orthogonal axes `u, v` from contact normal (handle degenerate case where normal ≈ (0,0,1)), generate 6 perturbed directions (60° steps, 2° tilt), project world-space support points to 2D contact plane
- [x] 3.2 Write `clipping.glsl`: implement Sutherland-Hodgman variant for convex polygon clipping (polygon A clipped by edges of polygon B), output clipped polygon as fixed-size array (max 16 vertices)
- [x] 3.3 Implement rotating calipers algorithm in `clipping.glsl`: if clipped polygon has > 4 vertices, find the 4 vertices forming maximum-area quadrilateral (brute-force C(N,4) for N ≤ 16)
- [x] 3.4 Wire perturbation + clipping into main shader: after MPR finds base contact, call perturbation manifold generation, output each manifold point as a separate result entry via `atomicAdd(count, num_points)`
- [x] 3.5 Each manifold point is a separate result entry with its own contact positions — up to 4 entries per collision pair
- [x] 3.6 Verify shader compiles and produces expected SPIR-V output

## 4. GPU Pair Generation Shader & Physics Example Integration

- [x] 4.1 Write `generate_pairs.comp`: compute shader that dispatches `max_pairs` threads, each thread computes its upper-triangle pair `(i, j)` from `gl_GlobalInvocationID.x` (row-accumulation formula), writes `uvec2(i, j)` to output SSBO; threads beyond N*(N-1)/2 early-return (done in Phase 1)
- [x] 4.2 Add `generate_pairs.comp` to `ConvexCollisionDetector::Impl` — create a second `ComputeStage` + `ComputeResourceBinding`, load SPIR-V from `solver/ConvexCollisionDetector/generate_pairs.comp.spv` (done in Phase 2)
- [x] 4.3 Create the collision pair buffer (`m_gpu_collision_pairs`, `uvec2`, size `max_collision_pairs`) owned by `ConvexCollisionDetector` (done in Phase 2)
- [x] 4.4 Wire pair-generation pass into `Step()`: import pair buffer as external resource, add pair-generation compute pass BEFORE collision detection pass, use `UseBuffer()` read/write declarations so the render graph inserts the barrier automatically (done in Phase 2)
- [x] 4.5 Update collision detection pass to read the pair buffer (instead of accepting externally-set pairs); remove `SetCollisionPairs()` method (done in Phase 2)
- [x] 4.6 In `PhysicsExampleRenderGraphBuilder.cpp`, wire collision detection before XPBD; keep XPBD solver passes enabled for model matrix updates; comment out XPBD `step.comp` body (Z-movement + debugPrintfEXT) as no-op placeholder
- [x] 4.7 Add collision detection pass to the render graph via `ConvexCollisionDetector::Step()` — the detector internally handles pair generation + detection
- [x] 4.8 Instantiating `ConvexCollisionDetector` with `max_collision_pairs` from shape count, wire into render graph builder
- [x] 4.9 Enable Vulkan debug printf extension in the example to capture `debugPrintfEXT` output from collision shader
- [x] 4.10 Build and run the example: verify no Vulkan validation errors, verify collision debug messages appear for overlapping boxes
- [x] 4.11 Verify expected collision counts: 2 overlapping boxes → 1 collision pair with manifold points output

## 5. Polish & Verification

- [x] 5.1 Review and clean up all debug printf messages — format strings are single-line, output is readable
- [x] 5.2 Verify no memory leaks: ComputeBuffer, ComputeStage, ComputeResourceBinding all properly destroyed via unique_ptr
- [x] 5.3 Verify edge cases: zero shapes (total_pairs==0 early-return in Step()), single shape (0 pairs, skipped), max pairs not exceeded (shader guards with collision_ids.v.length() check)
- [x] 5.4 Run existing physics tests (`test/physics_registration_test.cpp`) to confirm no regressions — builds and runs successfully
- [x] 5.5 Address any `#include` path issues or GLSL compilation warnings from Phase 1 — GL_GOOGLE_include_directive extension added, compilation succeeds
