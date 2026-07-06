## 1. Shader: generate_all_pairs_fallback.comp

- [x] 1.1 Add `readonly buffer AabbMin { vec4 v[]; } aabb_min;` and `readonly buffer AabbMax { vec4 v[]; } aabb_max;` at bindings 7 and 8 (appended after the existing 0–6 bindings).
- [x] 1.2 Add `aabb_overlap(uint a, uint b)` helper using 3-axis `greaterThanEqual` + `all` check on `.xyz`.
- [x] 1.3 In `main()`, after the `shape_alive` check and before the `is_filtered` check, add `if (!aabb_overlap(i, j)) return;`.
- [x] 1.4 Verify binding indices 7/8 do not collide with existing bindings (current top binding is 6 = `ShapeFilterData`).

## 2. Shader: generate_broad_pairs.comp

- [x] 2.1 Add `readonly buffer AabbMin { vec4 v[]; } aabb_min;` and `readonly buffer AabbMax { vec4 v[]; } aabb_max;` at bindings 12 and 13 (appended after the existing 0–11 bindings).
- [x] 2.2 Add the same `aabb_overlap(uint a, uint b)` helper.
- [x] 2.3 In the inner loop, after the `shape_alive` check on `shape_b` and before the canonical-order / `is_filtered` check, add `if (!aabb_overlap(shape_a, shape_b)) continue;`.
- [x] 2.4 Verify binding indices 12/13 do not collide (current top binding is 11 = `ShapeFilterData`).

## 3. Shader: generate_global_pairs.comp

- [x] 3.1 Add `readonly buffer AabbMin { vec4 v[]; } aabb_min;` and `readonly buffer AabbMax { vec4 v[]; } aabb_max;` at bindings 10 and 11 (appended after the existing 0–9 bindings).
- [x] 3.2 Add the same `aabb_overlap(uint a, uint b)` helper.
- [x] 3.3 In `main()`, after the `shape_alive[s]` check and the global×global dedup check, and before the `is_filtered` check, add `if (!aabb_overlap(g, s)) return;`.
- [x] 3.4 Verify binding indices 10/11 do not collide (current top binding is 9 = `ShapeFilterData`).

## 4. C++: SpatialHashBroadDetector.cpp — fallback pass

- [x] 4.1 In the `generate_all_pairs_fallback` SRB block, add `srb.BindBuffer("AabbMin", *gpu_aabb_min);` and `srb.BindBuffer("AabbMax", *gpu_aabb_max);`.
- [x] 4.2 In the `BH Fallback AllPairs` `RenderGraphPassBuilder` chain, add `.UseBuffer(aabb_min_h, RR)` and `.UseBuffer(aabb_max_h, RR)`.

## 5. C++: SpatialHashBroadDetector.cpp — within-cell pass

- [x] 5.1 In the `generate_broad_pairs` SRB block, add `srb.BindBuffer("AabbMin", *gpu_aabb_min);` and `srb.BindBuffer("AabbMax", *gpu_aabb_max);`.
- [x] 5.2 In the `BH Generate Pairs` `RenderGraphPassBuilder` chain, add `.UseBuffer(aabb_min_h, RR)` and `.UseBuffer(aabb_max_h, RR)`.

## 6. C++: SpatialHashBroadDetector.cpp — global pass

- [x] 6.1 In the `generate_global_pairs` SRB block, add `srb.BindBuffer("AabbMin", *gpu_aabb_min);` and `srb.BindBuffer("AabbMax", *gpu_aabb_max);`.
- [x] 6.2 In the `BH Global Pairs` `RenderGraphPassBuilder` chain, add `.UseBuffer(aabb_min_h, RR)` and `.UseBuffer(aabb_max_h, RR)`.

## 7. Build & verify

- [x] 7.1 Confirm `prev_access` on the `gpu_aabb_min` / `gpu_aabb_max` imports (`ImportExternalResource(..., RW)` at lines ~397-398) is unchanged — no edit needed, just verify.
- [x] 7.2 Build the engine (`mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug -G "MinGW Makefiles"` then compile); confirm all three modified shaders compile to `.spv` under `<build>/engine/Physics/spirv/collision/SpatialHashBroadDetector/`.
- [x] 7.3 Run a physics example with mixed shapes (some sharing cells, some global, some small-N fallback) and confirm no crashes / no validation-layer errors.
- [x] 7.4 RenderDoc capture: confirm the `compute_aabbs` → `generate_*_pairs` passes have the expected RAW memory barrier inserted between them for `gpu_aabb_min` / `gpu_aabb_max` (proving the `UseBuffer` declarations registered the dependency).
- [x] 7.5 RenderDoc capture: compare emitted `pair_count` before/after the change on the same scene to confirm the AABB pruning reduces pair count without dropping legitimately-colliding pairs (spot-check a few pairs in the narrow phase).
