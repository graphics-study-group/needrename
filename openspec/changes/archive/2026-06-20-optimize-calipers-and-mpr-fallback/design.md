# Design: Optimize Calipers and MPR Fallback

## Context

The collision detection pipeline runs entirely on GPU as compute shaders. After MPR finds a single contact point and normal, the perturbation phase generates a 2D contact manifold:

```
perturb_manifold()
  ├── 6 support() queries (every 60°, tilted 2°)
  ├── Incremental plane tracking (per shape, largest triangle normal)
  ├── 2D projection onto MPR contact plane
  ├── Sutherland-Hodgman clipping (A ∩ B in 2D)
  ├── [Step 7] rotating_calipers_reduce → ≤ 4 vertices  ← O(N⁴) brute force
  ├── Un-project to 3D fitted planes, validate depth
  └── [Step 9-10] MPR fallback:
      ├── if valid_count < 4: append MPR point
      └── if valid_count == 4: merge 5 → calipers → 4  ← 2nd calipers call!
```

Three problems exist: (1) `rotating_calipers_reduce` is O(N⁴) brute force despite its name, (2) calipers is called twice with fragile remapping logic in between, (3) the MPR fallback point can be discarded by the second calipers call.

The reference implementation in [`reference_clipping.py`](../../reference_clipping.py) demonstrates the canonical O(N) Rotating Calipers algorithm. This design ports that algorithm to GLSL and simplifies the fallback path.

## Goals / Non-Goals

**Goals:**
1. Replace O(N⁴) brute force with O(N) true Rotating Calipers in `clipping.glsl`
2. Eliminate the second calipers call by raising per-pair cap from 4 to 5
3. Guarantee MPR fallback point is never discarded
4. Update buffer sizing (C++ side) and dispatch sizing (GLSL side) for the 4→5 change

**Non-Goals:**
- Not changing MPR algorithm itself
- Not changing Sutherland-Hodgman clipping
- Not changing the perturbation ray pattern (6 rays at 60°)
- Not adding per-contact warm-starting for the XPBD solver
- Not refactoring the solver's Jacobi iteration pattern

## Decisions

### Decision 1: Port the true O(N) Rotating Calipers from reference_clipping.py

**Rationale**: The algorithm in `reference_clipping.py` is a textbook O(N) implementation that:
1. Walks antipodal pairs to find the hull diameter (one linear pass)
2. Scans once to find extreme points on both sides of the diameter (second linear pass)

This is O(N) total vs O(N⁴) brute force. For N=16, the brute force evaluates 1820 quadrilaterals; the true algorithm does ~48 signed-area computations.

**Alternatives considered**:
- Keeping brute force with better caching: still O(N⁴), doesn't fix the name mismatch
- Using convex hull + rotating calipers from scratch: unnecessary — the clipped polygon is already convex and CCW-ordered
- Pre-sorting by angle: unnecessary — the clipped polygon vertices are already in CCW order from Sutherland-Hodgman

**GPU divergence concern**: The inner `while` loop that advances the antipodal pointer could cause warp divergence. Mitigation: (a) the loop bound is at most N iterations total across ALL i (amortized O(1) per i), not per-thread divergence on a common path; (b) for the small N values here (≤16), the divergence cost is negligible; (c) we can structurally bound the loop to `N` iterations with a `for` loop and conditional break to ensure the compiler doesn't generate unbounded control flow.

### Decision 2: Raise max contacts per pair from 4 to 5

**Rationale**: The only case where we exceed 4 is the MPR fallback path (non-parallel fitted planes). By raising the cap to 5:
- We eliminate the second calipers call entirely — the MPR point is unconditionally appended
- We eliminate the fragile nearest-neighbor 2D→3D remapping (lines 349-358 of perturbation.glsl)
- We get better contact coverage for the non-parallel case

**Cost**: 25% more buffer memory for collision result buffers. For a scene with 100 shapes (4950 pairs), the buffer sizes go from `4950*4 = 19800` entries to `4950*5 = 24750` entries. Each entry is small (uvec2 + vec4 + vec4 + vec4 ≈ 52 bytes), so the memory increase is ~250 KiB — negligible.

**Alternatives considered**:
- Keep cap at 4 but ensure MPR point always selected: requires a 5→4 selection policy that prefers MPR, which is more complex than just allowing 5
- Dynamic buffer sizing based on actual contact count: GPU buffer allocation must be fixed at dispatch time, so dynamic sizing isn't feasible

### Decision 3: MPR fallback always appended, never reduced

**Rationale**: The MPR deepest point is the only contact point guaranteed to exist when MPR reports a collision. The perturbation points are best-effort refinements. When the fitted planes diverge (non-parallel contact, e.g., edge-on-face), the perturbation contact points may be geometrically less reliable than the MPR point. Discarding the MPR point via area optimization defeats its purpose as a fallback.

**Implementation**: In Step 10 of `perturb_manifold()`:
- If `valid_count < 4`: append MPR point (unchanged from current)
- If `valid_count == 4`: append MPR point as 5th point (changed — was "merge and reduce")
- If `valid_count == 5`: cannot happen with current logic (max 4 perturbation points pass validation), but guard with a bounds check

The removed code block (~60 lines of GLSL) is the combined-calipers + nearest-neighbor remapping logic.

## Algorithm Detail: O(N) Rotating Calipers

### Overview

The algorithm finds an approximate maximum-area quadrilateral from a convex CCW polygon in two linear passes:

```
┌─────────────────────────────────────────────────────────────────┐
│                    ROTATING CALIPERS (O(N))                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  PHASE 1: Find Diameter (p1, p3)                                │
│  ────────────────────────────────                                │
│                                                                  │
│  For each hull edge (i, i+1), advance antipodal pointer j        │
│  while the signed area to vertex j+1 increases.                  │
│  Track the (i, j) pair with maximum squared distance.           │
│                                                                  │
│                    hull[j]                                       │
│                      ●                                           │
│                     / \                                          │
│                    /   \      ← "caliper" parallel to edge (i,i+1) │
│                   /     \                                        │
│                  /       \                                       │
│      hull[i] ●───────────● hull[i+1]                            │
│                                                                  │
│  As i walks the hull, j follows — j only advances, never         │
│  retreats.  Total j advances ≤ N, so Phase 1 is O(N).           │
│                                                                  │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  PHASE 2: Find Extreme Points (p2, p4)                          │
│  ──────────────────────────────────────                          │
│                                                                  │
│  For each hull vertex k, compute signed area relative to         │
│  the diameter line (p1→p3).                                      │
│                                                                  │
│      p2 ●                ← max positive signed area              │
│         │\                                                       │
│         │ \                                                      │
│  p1 ●───┼──● p3     ← diameter                                  │
│         │ /                                                      │
│         │/                                                       │
│      p4 ●                ← max negative signed area              │
│                                                                  │
│  This is a single O(N) scan.                                     │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Phase 1: Antipodal Walking

**Goal**: Find the pair of hull vertices `(p1, p3)` with maximum Euclidean distance (the hull diameter).

**Algorithm**:

```
Input:  hull[0..N-1] — CCW convex polygon vertices (vec2)
Output: p1, p3 — indices of the diameter endpoints

1. p1 = 0, p3 = 1
   max_dist_sq = |hull[0] - hull[1]|²

2. j = 1  (antipodal pointer, starts opposite i=0)

3. For i = 0 to N-1:
   a. edge = (hull[i], hull[(i+1) % N])
   
   b. // Advance j while the next vertex is "further" from edge (i,i+1)
      while True:
          area_curr = signed_area(hull[i], hull[(i+1)%N], hull[j])
          area_next = signed_area(hull[i], hull[(i+1)%N], hull[(j+1)%N])
          if area_next > area_curr:
              j = (j + 1) % N
          else:
              break
   
   c. // Check antipodal pair (i, j)
      d_sq = |hull[i] - hull[j]|²
      if d_sq > max_dist_sq * (1 + ε):
          max_dist_sq = d_sq;  p1 = i;  p3 = j
   
   d. // Check antipodal pair (i+1, j)
      d_sq = |hull[(i+1)%N] - hull[j]|²
      if d_sq > max_dist_sq * (1 + ε):
          max_dist_sq = d_sq;  p1 = (i+1)%N;  p3 = j
```

**Key insight**: The `while` loop advances `j` at most N times total across the entire outer loop. Each vertex becomes the antipodal point for at most one edge before the pointer moves on. This is the classic "two-pointer" or "caterpillar" pattern — both `i` and `j` only move forward (mod N), so total work is O(N).

**Why check both (i,j) and (i+1,j)?** In the rotating calipers framework, when the caliper rotates to align with edge (i,i+1), the antipodal point is j. But (i+1,j) is also a valid antipodal pair — "caliper through vertex i+1, parallel to edge through j." Both pairs define potential diameter endpoints.

### Phase 2: Bilateral Extreme Points

**Goal**: Given the diameter (p1,p3), find p2 and p4 — the vertices furthest from the diameter line on each side.

**Algorithm**:

```
Input:  hull[0..N-1], p1, p3 (from Phase 1)
Output: p2, p4

1. p2 = 0, p4 = 0
   max_area_1 = 0.0   (positive side)
   max_area_2 = 0.0   (negative side)

2. For i = 0 to N-1:
   area = signed_area(hull[p1], hull[p3], hull[i])
   
   if area > max_area_1 * (1 + ε):
       max_area_1 = area;  p2 = i
   elif -area > max_area_2 * (1 + ε):
       max_area_2 = -area;  p4 = i
```

**Why this works**: The area of triangle `(p1, p3, i)` is proportional to the perpendicular distance from vertex `i` to the diameter line. The vertex maximizing this distance on each side contributes the most area to the quadrilateral. The quadrilateral `(p1, p2, p3, p4)` approximates the maximum-area quadrilateral — it is the convex hull of the diameter endpoints and the two furthest side points.

### Tie-breaking Epsilon

All comparisons use scale-invariant relative epsilon:

```glsl
const float TIE_EPSILON_REL = 1.0e-3;

// Instead of: if (candidate > best)
// Use:        if (candidate > best * (1.0 + TIE_EPSILON_REL))
```

This prevents floating-point oscillation when multiple antipodal pairs have nearly identical distances (common with circular/symmetric geometry). Scale invariance means the epsilon adapts — a 1-meter object tolerates ~1mm ambiguity; a 0.01-meter object tolerates ~0.01mm.

### GLSL Implementation Notes

The algorithm ports directly to GLSL with these adjustments:

1. **Signed area**: Already exists in `clipping.glsl` as `signed_area_2d(a, b, c)` — returns `(b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x)`.

2. **While loop → counted loop**: GLSL compilers handle `while(true)` with `break` well, but for safety we can use a counted loop:
   ```glsl
   for (uint step = 0u; step < vertex_count; step++) {
       // compute area_curr, area_next
       if (area_next > area_curr) {
           j = (j + 1u) % vertex_count;
       } else {
           break;
       }
   }
   ```
   The bound of `vertex_count` is tight: `j` advances at most N times total.

3. **Distance computation**: `d.x*d.x + d.y*d.y` — avoids `sqrt()` since we only compare.

4. **Modulo operation**: `(i + 1u) % vertex_count` — GLSL supports integer `%` natively. For the hull, `vertex_count` is known at compile time to be ≤ MAX_CLIP_VERTS (16).

### Complexity Comparison

```
┌──────────────────────┬──────────────────┬────────────────────────┐
│ Step                 │ Brute Force      │ True Rotating Calipers │
├──────────────────────┼──────────────────┼────────────────────────┤
│ Diameter find        │ (bundled in C4)  │ ≤ 2N signed areas      │
│ Extreme points       │ (bundled in C4)  │ N signed areas         │
│ Quad area eval       │ C(N,4) times     │ 1 time (at output)     │
│ Total operations     │ C(N,4) × 1 quad  │ ~3N signed areas + 1   │
│                      │   area each      │ quad area              │
│                      │                  │                        │
│ N=6:                 │ 15 area evals    │ ~18 area evals         │
│ N=12:                │ 495 area evals   │ ~36 area evals         │
│ N=16:                │ 1820 area evals  │ ~48 area evals         │
└──────────────────────┴──────────────────┴────────────────────────┘
```

For N≤6 (typical after clipping), the difference is negligible. For N=12-16 (complex intersections), the true calipers is 10-38× faster in operation count.

## Data Flow Changes

### Before (current):

```
S-H clip → calipers#1 → 4 verts → validate → 4 valid
                                              │
                    ┌─────────────────────────┘
                    │ (MPR fallback, valid_count==4)
                    ▼
               merge 5 → calipers#2 → nearest-neighbor remap → 4
```

### After (proposed):

```
S-H clip → calipers (O(N)) → 4 verts → validate → ≤4 valid
                                                    │
                        ┌───────────────────────────┘
                        │ (MPR fallback)
                        ▼
                   append MPR → ≤5 valid  (no reduction)
```

### Buffer sizing changes:

```
Before:  max_collision_pairs = min(total_pairs * 4, max_contact_points)
         max_contacts = max_pairs * 4

After:   max_collision_pairs = min(total_pairs * 5, max_contact_points)
         max_contacts = max_pairs * 5
```

## Risks / Trade-offs

- **[GPU occupancy] 5× buffer multiplier vs 4×**: 25% more GPU memory for collision result buffers. For typical scenes (<100 convex shapes), this is ~250 KiB additional memory — negligible on any Vulkan-capable GPU.

- **[Solver cost] 5 contacts per pair vs 4**: The XPBD solver processes each contact independently in parallel. An extra contact per pair means at most 25% more solver work for pairs that trigger the MPR fallback. The fallback only triggers when fitted planes diverge by >0.1°, which is the minority case in most simulations.

- **[Warp divergence in while loop]**: The antipodal-advance `while` loop may cause divergence across lanes in a warp. Mitigation: (a) the amortized number of advances per iteration is ≤1, (b) the bounded `for` loop variant ensures worst-case N iterations, (c) for N≤16, the divergence penalty is dwarfed by the 10-38× reduction in total operations.

- **[Determinism]**: The relative tie-breaking epsilon (1e-3) ensures deterministic output even with floating-point rounding. This is already battle-tested in the Python reference implementation.

- **[Backward compatibility]**: No public API changes. `ConvexCollisionDetector` constructor signature is unchanged. `XPBDGpuSolver` public interface is unchanged. Only internal buffer sizing and shader logic change.

## Open Questions

1. **Should `tie_epsilon_rel` be configurable?** Currently hardcoded at 1e-3 in the Python reference. For the GPU port, we may want it as a `const float` in the shader (same approach as `CLIP_EPSILON`). Not worth a uniform buffer parameter unless real-world scenes show instability.
