## Context

The contact solver runs two phases per substep, both in the Jacobi accumulate/reduce/apply shape:

- **Position phase** (`accumulate_contact_position.comp` → `SumByKey` → `apply_body_position_deltas.comp`), `num_iter_persubstep` iterations (50 in the physics app config). Solves the normal penetration constraint and accumulates the normal multiplier `lambda_n` into `ContactLagrange`.
- **Velocity phase** (`accumulate_contact_velocity.comp` → `SumByKey` → `apply_body_velocity_deltas.comp`), `num_velocity_iters` iterations (10 in the app config), after `update_velocities_from_pose.comp` has replaced every velocity with `v = (p_post - p_start) / dt`. Solves restitution and friction from the position phase's `lambda_n`.

Constraints this design has to respect:

- **A slot is written, never accumulated** (`common/xpbd_scatter.glsl`): one entry slot is written once per invocation, so several contributions for the same contact must be summed into one impulse before scattering. The impulse-to-delta map and the relative-velocity map are both linear in the impulse, so summing is exact.
- **Bindings are consecutive from 0** (`physics-push-constants`, "Shader bindings are consecutive"), so new bindings append at the end of each shader's list.
- **No new shader files** (`physics-gpu-shaders` pins the loaded `.spv` list), and no push-constant block changes: the contact-point capacity is `entry_capacity / 2`.
- **`dt` is the substep dt** (`push_gravity_dt.w = time_step / num_substep_perstep`, h = 1/120 s in the app config), which is what makes `lambda_n / dt` an impulse: for a resting 1 kg box it reproduces the weight impulse `m * g * h`.
- **The substep-start snapshots are pre-integration.** `gpu_substep_start_position` / `gpu_substep_start_orientation` are copied before `integrate_forces.comp` runs, and `update_velocities_from_pose.comp` uses them for `v = (p_post - p_start) / dt`. They are the solver's `x_prev` in the reference XPBD scheme.
- **Contact capacity**: `max_contact_point = max(1, min(all_pairs * 5, config.max_contact_points))`, entry capacity `2 * max_contact_point`. The per-contact multiplier buffers are sized to `max_contact_point`, and that same number is the narrow detector's write budget, so an over-budget contact index is unrepresentable in either component.
- **Detector count vs budget**: `detect_collisions.comp` reserved `num_points` with one `atomicAdd` *before* its per-write budget check, so the published count could exceed the budget; nothing clamped it afterwards, and the count is read unguarded by the entry pass.
- **No atomic update is obtained by retrying** (`gpu-kernel-atomics`): a compare-and-swap spun until it succeeds makes a kernel's progress depend on contention, and a bounded attempt count turns that into a silent loss of work at exactly the contention the loop exists to handle.

Three facts established while diagnosing this are load-bearing:

1. **Every constraint direction has its own inverse effective mass.** With `L(d) = (inv_mass_a + inv_mass_b) * d + (I_inv_a * (r_a x d)) x r_a + (I_inv_b * (r_b x d)) x r_b`, the relative-velocity change delivered by an impulse `p * d` is `L(p * d)`, and `dot(d, L(d)) = w(d)`. `w(d)` varies with direction through `|r x d|^2 = |r|^2 - (r . d)^2`, so it is direction-independent only when the lever arms vanish or the coupling is isotropic; for a long bar contacting at its end, `w(n) / w(t)` is about 3.9.
2. **The contact normal points from body A toward body B.** The MPR portal normal is the outward normal of the nearest CSO face (`mpr.glsl`, "ensure portal normal points away from V0"), and the perturbation un-projection gives `dot(pt_b - pt_a, n) < 0` for overlap. Both agree with the position shader's `C = dot(contact_a - contact_b, n) > 0` penetration gate and with `vn = dot(n, rel_vel) > 0` meaning approaching. The comments in `mpr.glsl`, `perturbation.glsl` and the existing spec requirement say "from B to A", which is wrong.
3. **A detected contact pair has a *zero* tangential offset, and that is why the drift reference must be the substep start.** Both points of a pair are un-projected from one contact-plane coordinate `(u, v)` along one normal onto each shape's fitted plane, so `pt_a - pt_b` is parallel to `n` by construction (the MPR fallback returns a barycentric combination of CSO vertices whose difference is the penetration vector, also parallel to `n`). The detector therefore re-anchors the pair to the post-integration geometry on every substep and the sliding that happened during force integration is invisible to any measurement taken at detection time.

## Goals / Non-Goals

**Goals:**

- Friction whose strength depends only on the friction coefficients, the normal impulse and the geometry — not on body mass, not on `num_velocity_iters`.
- Both halves of Coulomb friction: static friction that removes the substep's tangential drift at the position level, kinetic friction that decelerates sliding at the velocity level, with each phase owning exactly one coefficient and one per-substep budget, and no double charging.
- Contact and joint constraints that treat kinematic bodies as immovable, apply the normal correction only where the position phase actually solved the contact, and never index past the contact-point capacity.

**Non-Goals:**

- No change to the collision detection algorithm, the manifold generation, the entry/sort/reduce pipeline, the scratch layout, the push-constant blocks, or the shader file list.
- No rolling friction, no anisotropic friction, no friction anchors or contact patches, no warm starting across substeps, no contact caching.
- No change to the Jacobi averaging in either apply pass (it is a specified design, not a defect).
- No change to the restitution model: the "cancel the separation velocity of a contact that is still detected" behaviour is pre-existing and deliberate in the velocity-from-pose scheme, and re-specifying it is a separate change.

## Decisions

### D1: Convert each constraint direction with its own effective mass

The velocity phase computes `w_n` with `n` and `w_t` with `t`, forms the normal impulse `(normal_delta_v / w_n) * n`, and adds the kinetic friction difference to it (D15) before the existing scatter. The blended `correction_dir` and its single `denom` are removed. The position phase does the same for its normal part (`w_n`) and its tangential part (`w_t`).

*Why:* the single-mass form is exact only along the blended direction; decomposed onto `n` and `t` it under-delivers the normal target and mis-scales the tangential impulse by `w(t) / w(d)`, and it cannot express the cross-coupling between the two directions (the outer iterations are what converge that coupling; each direction must be solved on its own for them to do so).

*Alternatives considered:* keep the blended direction and iterate more (rejected: the fixed point is wrong, not merely slow); solve the contact as a 2x2 block (rejected: more code, non-standard for a Jacobi GPU pass, and the cross terms are small enough for outer iterations).

### D2: Compare the Coulomb bound in impulse space

`p_normal = |lambda_n| / dt` is an impulse. The bound is applied to the substep's **total**: `ContactTangentImpulse` holds that total, the contact's wanted total is `p_friction - (length(tangent_vel) / w_t) * t`, and it is projected onto the disc of radius `mu_d * p_normal` (D15). The resulting velocity change is the scattered difference times `w_t`.

*Why:* Coulomb friction bounds an impulse, not a velocity. Comparing the impulse bound directly against a velocity omits the `w_t` factor, which is the reduced mass in the common case — so friction strength scales with mass (about x2 too weak at 1 kg, x50 too strong at 100 kg, x200 too weak at 0.01 kg). The reference XPBD scheme states the same bound in force terms (`f_n = lambda_n / h^2`, bound `h * mu_d * |f_n| = mu_d * lambda_n / h`, an impulse), and every engine surveyed clamps in impulse space.

### D3: Per-substep accumulated friction budgets, in two new per-contact buffers

Two solver-owned `vec4` buffers sized `max_contact_point` (one vector per contact point, `.xyz` used), cleared once per substep beside `ContactLagrange` by the existing flat clear (`clear_int_buffer.comp` writes the integer `0`, whose bit pattern is `+0.0f`, which is exactly how `ContactLagrange` is already cleared; the clear is element-counted in ints, so it covers `4 * max_contact_point`):

- `ContactTangentLagrange` (`kg*m`, position phase): the vector `lambda_t`, tested against `mu_s * lambda_n` by `length(...)`.
- `ContactTangentImpulse` (`kg*m/s`, velocity phase): the vector `p_friction`, holding the substep's total, projected onto the disc of radius `mu_d * |lambda_n| / dt` (D15).

*Why:* the accumulated budget has to span the iterations of its phase, because the Coulomb bound is a per-substep bound. Units stay in each phase's natural space, so no conversion appears in either clamp, and each phase owns one coefficient. Both are vectors because the constraint direction is recomputed every iteration and rotates — see D14.

*Alternatives considered:* one shared buffer reinterpreted per phase (rejected: same memory, worse readability, two units and two owners); a single budget shared across the phases (rejected: see D4); accumulating a scalar magnitude per contact (rejected: see D14 — the cone bounds the vector sum, and a magnitude accumulator silently abstains once self-cancelling corrections have spent the budget).

### D4: Static friction in the position phase, measured from the substep-start pose

The position phase solves `C_t = |dp_t|` with

```
pt_start_i = substep_start_pos_i + quat_rotate(substep_start_ori_i, local_in_body_i)   // same body-local point as the current pose
dp         = (contact_a - pt_start_a) - (contact_b - pt_start_b)
dp_t       = dp - dot(dp, n) * n
```

direction `t = dp_t / |dp_t|`, its own effective mass `w_t`, and the budget rule `dlambda_t = |dp_t| / w_t`, applied only if `length(lambda_t + dlambda_t * t) <= mu_s * lambda_n`, where `lambda_t` is the accumulated tangential multiplier *vector* of D14.

*Why the reference is the substep start and not the detection-time contact points:* the detector derives both points of a pair from one contact-plane coordinate along one normal, so their tangential offset is identically zero at detection. A detection-time reference therefore measures nothing, the static constraint degenerates to a no-op, and the substep's integrated tangential displacement stays uncorrected. That displacement is exactly the creep this feature exists to fix: a body resting on a slope gains `g * sin(theta) * h` of tangential velocity during integration and moves `g * sin(theta) * h^2` before collision detection runs, which at h = 1/120 s and 20 degrees is about 2.8 cm/s of steady creep. The substep-start reference covers the whole substep, mirroring the velocity update `v = (p_post - p_start) / dt`, and the same two buffers are reused (no new snapshots, no new passes).

*Why each phase gets one coefficient:* the position phase is the static (stick) constraint and the velocity phase is the kinetic (slip) constraint. Partitioning rule: the position phase applies its correction only if the required multiplier fits the remaining static budget; otherwise it skips that contact for that iteration. Then a sticking contact's drift is cancelled — the pose-derived tangential velocity becomes zero and the velocity phase's unconditional kinetic correction is a no-op for it — while a sliding contact receives the kinetic friction from the velocity phase with `mu_d`, so the substep total is the kinetic bound, not the static one and not their sum.

*Alternative considered and rejected:* apply a partial static correction and subtract what it used from the velocity phase's budget. When sliding, the static correction saturates at `mu_s * lambda_n`, so the remaining budget is `(mu_d - mu_s) * p_n`, which is zero or negative whenever `mu_s > mu_d`: the effective friction coefficient becomes the *static* one and the body decelerates as if `mu_d == mu_s`. Skipping avoids that.

*Note on the newly detected contact:* for a pair detected in the substep it first appears, the substep-start pose puts its material points where they were before the approach, so `dp_t` includes the tangential part of the approach displacement. That is intended — it is real relative sliding during the substep — and it is bounded by the same cone, which is the standard friction-on-impact behaviour.

### D5: Kinematic bodies are immovable, established at the mirror and restated in the shaders

`PhysicsScene::SubmitRigidBody` writes `0` mass and a zero `inverse_inertia` for a kinematic body; every constraint accumulate shader additionally computes `inv_mass = (is_kinematic == 0 && mass > 1e-12) ? 1 / mass : 0` and omits the kinematic body's `I_inv` term wherever `w(d)` is built. The authored values stay in the component and the descriptor.

*Why both layers:* the mirror rule fixes every present and future consumer of the SoA columns in one place (including the hinge and fixed joint denominators, which have the same defect as the contact shaders, and the `inv_mass_a <= 0 && inv_mass_b <= 0` early-out, which today does *not* fire for two kinematic bodies because their authored masses are non-zero). The shader rule keeps the constraint semantics independent of the upload path. `rigid_body_mass`, `rigid_body_inertia` and `rigid_body_inverse_inertia` are upload-only — no host code reads them back — and `is_kinematic` is written only by `SubmitRigidBody`, so the mirror rule is closed: no path can mark a body kinematic without passing through it.

*Why not at submission only:* a future constraint shader could forget the flag; *why not in the shaders only:* the joint shaders already forgot it, and any other consumer of the columns would have to repeat the rule.

### D6: The normal correction is gated on `lambda_n > 0`

The velocity phase applies the normal part only for contacts the position phase actually solved.

*Why:* `perturbation.glsl` keeps manifold points whose separation is within `contact_margin` and clamps their reported depth to 0. Those points are in the contact list but are skipped by the position phase (`C <= 0`), so their multiplier stays zero, yet the velocity phase currently cancels their normal velocity — stopping an approaching body at the margin instead of at the surface. `lambda_n > 0` is the exact predicate "the position phase pushed these bodies apart through this contact".

*Known limitation, out of scope:* a contact that is still detected while separating (typically one or two substeps after a bounce) has its separation velocity cancelled, because its `lambda_n` is positive. This is pre-existing behaviour and part of how the velocity-from-pose scheme removes the de-penetration velocity; changing it (a unilateral push test, or gating on `vn_prev > 0`) would trade bounce damping for energy injection on deep overlaps and needs its own change and its own bounce-apex test.

### D7: Index bound in the accumulate shaders, and a cursor/published split for the detector count

Both contact accumulate shaders return for `cidx >= entry_capacity / 2` (the contact-point capacity) in addition to the existing count guard. The detector carries the two roles of its collision count in two counters: a `ContactCursor` binding that allocates slot ranges with one `atomicAdd` and may run past the budget, and `collision_count`, raised by every thread that got a usable range with one `atomicMax` to `base + admissible`. The detector's contact capacity is the solver's `max_contact_point`, passed in through `BindToScene`, so the result buffer length and the write budget are one number instead of two derivations.

*Why all three:* the accumulate-side bound is the enforcing guard — it is what makes an out-of-range `ContactLagrange` / `ContactTangentLagrange` / `ContactTangentImpulse` write impossible. The detector-side fix removes the stale-geometry reads at their source. Reachability of the original defect: the count exceeds the budget whenever the points written exceed `config.max_contact_points`, and the theoretical ceiling is `5 * candidate_pairs` (the app configures 50000, so a scene of roughly 150+ fully-contacting shapes reaches it by that ceiling). The ceiling is loose in practice — scenario F of `xpbd_contact_friction_test` measures 55 mutually overlapping box pairs producing 40 contact points, about 0.7 per pair rather than 5, so the realistic threshold is several hundred collision shapes. The defect is real but was rarer than the ceiling suggests; the entry pass reads `collision_ids` past the end of its allocation for every index in `[capacity, 2 * capacity)` once it is reached.

*Why the count is derived rather than bounded:* an atomic add cannot be given a ceiling, so a cursor that hands out ranges necessarily overshoots. The obvious repair — read the count, compute the admissible remainder, then add that — is not atomic: two threads can both read a value below the budget and both add, publishing a count past it and over slots no thread wrote. The count can instead be *derived* from the ranges actually handed out, because a thread truncated by the budget knows the exact end of the written prefix (it proposes the budget, and every slot below it is filled, the ranges being disjoint and contiguous) and a thread that was not truncated proposes the end of its own range. The maximum of those proposals is exactly `min(cursor, budget)` under every interleaving — one `atomicMax`, no retry, no dropped work, and `collision_count` keeps the literal meaning the spec gives it.

*Alternative considered and rejected:* spinning on `atomicCompSwap` until the read/admit/write-back sequence lands. It makes progress depend on contention, needs an arbitrary attempt limit, and silently drops a thread's contacts when that limit is exhausted — a load-dependent correctness failure in the very case the limit exists to survive. Also rejected: letting the count overshoot and clamping it in every consumer, which needs a push-constant change in the entry pass (forbidden by D10) and makes correctness depend on every future consumer remembering to clamp.

### D8: Kinematic bodies skip the whole angular integration block

The integration shader returns before the angular section for a kinematic body, rather than zeroing the torque and continuing.

*Why:* the Coriolis term is subtracted after the torque is zeroed, so a kinematic body's only torque becomes `-cross(wb, I_local * wb)`; its angular velocity then drifts and its spin axis precesses whenever the spin is not along a principal axis of a non-isotropic inertia tensor. Kinematic motion is authored, so the solver must not integrate it.

### D9: Correct the normal-direction wording

The spec requirement and the comments in `mpr.glsl` and `perturbation.glsl` say the normal points from B to A; the computed value points from A to B (evidence in Context). The wording is corrected in the requirement being modified anyway, and the two shader comments in the same change.

*Why it matters:* every sign in the position and velocity phases — the `C > 0` gate, the restitution `min(-e * vn_prev, 0)` term, the scatter signs for each side, and the friction direction — is derived from this convention.

### D10: No new shaders, no push-constant changes, bindings appended

Static friction lives inside `accumulate_contact_position.comp`; the corrected kinetic friction stays inside `accumulate_contact_velocity.comp`; the reservation change stays inside `detect_collisions.comp` and adds the `ContactCursor` binding there rather than a publish pass. New bindings append after each shader's existing last binding: `RigidBodyStaticFriction`, `ContactTangentLagrange` and the two substep-start snapshots in the position shader; `ContactTangentImpulse` in the velocity shader; `ContactCursor` (binding 12) in the detection shader. The contact-point capacity is derived as `entry_capacity / 2` in the solver's shaders, so no push-constant block changes and the existing C++ push structs stay valid.

*Why not a publish pass:* the cursor/published split needs no new `.spv` and no new dispatch — the detector clears both counters with the flat clear kernel it already dispatches, and one `atomicMax` in the detection shader publishes the bound. A separate one-invocation clamp kernel would be the obvious alternative, and it is exactly what the pinned `.spv` list forbids.

### D11: The static constraint is bounded by the *remaining* budget, not by the reference scheme's literal guard

The reference XPBD text prints the static friction guard as "apply `dx = dp_t` only if `lambda_t < mu_s * lambda_n`" and prints no update law for `lambda_t`. Read literally, the guard depends only on the multiplier accumulated *before* this iteration, so a large drift (a fast slide) would be cancelled in full as long as the previous accumulated value was under the cone — an unbounded correction. This design instead requires the *increment* to fit the remaining budget (`length(lambda_t + dlambda_t * t) <= mu_s * lambda_n`, the vector form of D14), which keeps `length(lambda_t) <= mu_s * lambda_n` at all times and degenerates to the printed guard whenever the increment fits.

*Why:* it is the bounded reading of the same statement, it makes the cone an invariant rather than a check, and it is what makes the sliding case fall through to kinetic friction.

### D12: The kinetic correction is unconditional

The velocity phase applies the kinetic friction correction to every contact the position phase solved, whether or not that contact's static constraint was applied or skipped.

*Why:* it is what the reference scheme does (the dynamic correction is applied once per substep to every contact, with no test), it is what makes "a sliding contact always has friction" true, and it costs nothing for a sticking contact because the position phase has already driven its tangential velocity to zero (the correction's `min` against `length(tangent_vel) / w_t` makes it exactly zero).

### D13: Both collision detectors join the exported class list

`SpatialHashBroadDetector` and `ConvexCollisionDetector` are declared with `PHYSICS_API`, extending the set `physics-dll-module` pins.

*Why:* the count bound this change establishes is detector-owned state. `collision_count` and the contact capacity live in the detector's result buffers, and no exported solver API reaches them — the solver keeps its detectors private by design. Without the export the bound could only be reviewed, never asserted, and the change would be shipping a concurrency invariant with no executable evidence. The exported surface stays small: two classes whose constructors, `BindToScene`, `Record` and `GetResultBuffers` are the only members a test needs.

*Alternative considered and rejected:* adding a `GetCollisionResultBuffers()` accessor to `XpbdGpuSolver`. It would leak the detector's buffers through the solver's interface, making the solver responsible for a contract it does not own, and it would still leave the detector's own API untestable.

### D14: Both tangential accumulators are vectors, not sums of magnitudes

`ContactTangentLagrange` and `ContactTangentImpulse` each hold one `vec4` per contact point (`.xyz` used). The position phase's gate is `length(lambda_t + dlambda_t * t) <= mu_s * lambda_n` and it accumulates `lambda_t += dlambda_t * t`; the velocity phase projects its running total onto the cap's disc (D15), which keeps `length(p_friction)` at or below the cap by construction.

*Why:* the cone and the cap bound a contact's accumulated tangential *impulse*, whose direction is the drift/sliding direction `t`. `t` is recomputed every iteration from the surviving drift or sliding velocity, and it rotates whenever the other constraints (a second contact on the same body, a joint, the angular part of another correction) move the material points. The accumulated vector and the sum of the magnitudes are different quantities, and `length(vector sum) <= sum of magnitudes` by the triangle inequality, so a magnitude accumulator enforces a strictly stronger condition than the cone: it can never leave the cone — the direction of that error is safe, which is why the original scalar version was stable and why every measured slope, slide and stack number was believable — but self-cancelling corrections still consume the budget, and once the budget is spent the position phase's gate rejects every later iteration (`dlambda_t > 0` whenever the contact still drifts), silently withdrawing a sticking contact's static friction for the rest of the substep while its net tangential impulse is still far inside the cone.

*Why not clamp the vector to the cone instead of skipping:* that is position-level sliding friction, which would charge the same motion in both phases and contradicts D4's partition and the "a sliding body is not held by static friction" scenario. The stick-or-skip semantics are unchanged; only the quantity being tested changed. The velocity phase is the opposite case: its job is to deliver up to the cone for a contact that is already sliding, so it *does* project rather than skip (D15) — and that cannot double-charge, because the total it keeps inside the cap is the whole of what the contact receives.

*Behaviour change:* none for a contact whose drift direction is constant across its phase's iterations (straight-line sliding, a box on a ramp, the flat-plane scenarios) — the vector test degenerates to the magnitude test there. It is observable only when the direction rotates, and then only in the direction of *more* friction: the vector test is at least as permissive as the magnitude test, so it can only recover static friction the scalar version had spuriously dropped, never apply more than the cone allows.

*Cost:* the two buffers grow four-fold (still `O(max_contact_point)`), the two per-substep clears cover `4 * max_contact_point` int elements instead of `max_contact_point`, and no binding, push constant, `.spv` file or dispatch changes.

*Measured (tasks 9.7/9.8):* an isolated build that keeps the `vec4` buffers and their clears but restores the scalar arithmetic is indistinguishable from this one on every scenario of `xpbd_contact_friction_test` — flat-plane reduction, mass / iteration / direction invariances, the slope stick-slip bracket, the over-budget contact count, and the whole spinning-box scenario — to every printed digit. The single number that differs is the no-creep residual, `1.33787e-05 m` scalar against `1.61418e-05 m` vector over 600 substeps: 22 nm to 27 nm per substep against a per-substep drift of about `1.06e-03 m` that the position phase cancels, so the difference sits in the fifth significant digit of that cancellation and stays 60x inside the scenario's 1 mm assertion. The spinning-box bit-identity is evidence about that scene, and it is stronger than "the direction is constant": the two accountings differ only in how they aggregate the per-iteration increments, so bit-identity means every increment was collinear with the running total, which is what an accumulator that stops accumulating after its first iteration produces — either because the kinetic cap binds and the increment form's remainder goes to zero, or because the sliding is cancelled and there is nothing left to correct. Either way those scenes give the two accountings nothing to disagree about, so this decision removes a latent under-friction mode rather than changing a measured behaviour. D15 restores the one thing the increment form could not express — the total following a turning sliding direction — and the spinning-box scenario is the natural test for it.

### D15: The kinetic Coulomb bound is a projection of the accumulated total

`accumulate_contact_velocity.comp` forms the contact's wanted total for the substep, `p_ideal = p_friction - (length(tangent_vel) / w_t) * t`, projects it onto the disc of radius `mu_d * |lambda_n| / dt`, then scatters `p_new - p_friction` and stores `p_new`.

*Why:* the Coulomb bound is a statement about the impulse a contact receives over the substep, so it has to be applied to the accumulated total. Bounding the iteration's increment by the cap's remainder — `p_t <= mu_d * p_normal - length(p_friction)`, the form this decision replaces — is only a *sufficient* condition for staying inside the cone, and the triangle inequality makes it strictly stronger than the cone. Its failure mode is not a lack of safety but a lock-up: `p_friction` can grow towards the cap and effectively cannot unwind, so once its length reaches the cap the remainder is zero, every later iteration scatters nothing, and the total stays frozen in the direction the first iterations established. A contact whose sliding direction turns inside the substep — a spinning or rolling body, or a contact another constraint also pushes — then keeps receiving friction aimed along the old direction, which is exactly what the per-direction effective masses of D1 exist to get right.

*Why the projection is not merely permissive:* it preserves both properties the increment form was chosen for, and makes that form's own `min` guard unnecessary. The cone holds by construction, `length(p_new) = min(length(p_ideal), cap)`. And the correction never reverses the sliding: with `p_stop = length(tangent_vel) / w_t`, an unclamped total scatters exactly `-p_stop * t`, cancelling the sliding without overshooting, while a clamped one has `length(p_ideal) > cap >= length(p_friction)`, hence `p_stop > 2 * dot(p_friction, t)`, hence `dot(p_new - p_friction, -t) - p_stop = (dot(p_friction, -t) + p_stop) * (cap / length(p_ideal) - 1) <= 0`, the two factors being positive and negative.

*Why it also fixes the bookkeeping:* the scattered impulses telescope — each iteration scatters `p_new - p_friction` and stores `p_new` — so the substep total the contact receives is exactly the final `p_friction`, which is what the cap bounds. The increment form makes the substep total the sum of the magnitudes, which equals the final accumulator only while the direction is fixed.

*Behaviour change:* none for a steady slide, where the direction does not turn: the first iteration either cancels the sliding (`p_stop <= cap`) or saturates the total at the cap, and every later iteration is a no-op under both forms. It changes a slide whose direction turns within the substep, where the projection rotates the total with the motion and may unwind it. The spinning-box scenario of `xpbd_contact_friction_test` binds its cap and rotates, so it is the first scene here that can tell the two forms apart; the flat-plane / mass / iteration / direction scenarios are the no-change guard. This replaces the increment form the spec's velocity steps 3-4 previously stated, and is recorded in `tasks.md` section 9.

## Evidence

The table records what each surveyed solver clamps (all primary sources, code or papers), because the original defect is precisely a wrong answer to that column.

| Solver | Where friction is solved | Clamped quantity | Static/dynamic | Order in an iteration | Coefficient mixing |
|---|---|---|---|---|---|
| Box2D 2.4.1 | velocity only | accumulated tangential impulse vs `mu * accumulated normalImpulse`; principle stated as "the accumulated impulse is clamped and not the incremental impulse" | single coefficient | friction before normal ("non-penetration is more important") | geometric mean |
| Box2D 3.x | velocity only (no position projection) | accumulated | single | normal, then friction, in the relax iteration | geometric mean |
| Bullet | velocity (split impulse is penetration-only) | limits refreshed from the accumulated normal impulse, but the clamp is on `m_appliedImpulse` | single | normal row, then friction using its accumulated impulse | product, clamped to 10 |
| Jolt | velocity; position pass is non-penetration only | accumulated tangential lambdas projected to `mu * sum(accumulated non-penetration lambda)`; comment: "using the non-penetration impulse from the previous iteration here" | single | friction first, then non-penetration | geometric mean |
| PhysX 5 | velocity ("position iterations" are biased velocity solves) | accumulated friction force vs `mu * accumulated normal impulse` (patch-accumulated, distributed to anchors) | **static/dynamic switch**: stick iff `|p_t| <= mu_s * N_accum`, else clamp to `+/- mu_d * N_accum` and flag `broken` | normal, then friction | average (default) |
| MuJoCo 3 | constraint force level (solver variable is the total force) | projection of the *total* tangential force onto the cone, bounded by the same block's total normal force | single per geom (torsional/rolling are extra axes) | normal, then friction with the normal held | max |
| Rapier | velocity per substep | accumulated impulse, `cap_magnitude(mu * normal impulse)` | single | normal, then friction | average (default) |
| Müller 2020 (XPBD rigid bodies) | static at position level, dynamic at velocity level | static: accumulated `lambda_t` guarded against `mu_s * lambda_n`; dynamic: `min(h * mu_d * |f_n|, |v_t|)` with `f_n = lambda_n / h^2`, i.e. the impulse `mu_d * lambda_n / h`, applied once per substep | both, as two independent layers (not a branch) | normal first (the guard consumes `lambda_n`) | `(mu1 + mu2) / 2` |
| Small Steps 2019 | position level, one iteration per substep | `dlambda_f <- min(mu * dlambda_n, dlambda_f)` — the *increment* against the *increment*, which is the substep total only because that scheme runs one iteration per substep | single | flat "for all constraints" loop | single `mu` |
| Avian (open-source XPBD) | velocity | accumulated impulse clamped to `+/- friction * normal_impulse` | single | per contact part | single `mu` |

Takeaways that shaped the decisions:

- **Every solver clamps or projects the accumulated/total quantity; none re-grants a bound per iteration** (D3). Small Steps is the one apparent exception, and it is not one: it clamps increments against increments and runs exactly one iteration per substep, so the increment *is* the substep total. Clamping a per-iteration increment against an *accumulated* normal multiplier — the original defect — re-applies friction every iteration and overshoots the cone, and none of the sources states that explicitly; they simply never do it.
- **Only PhysX splits the coefficients**, and its stick test is the same predicate this design uses at the position level (needed impulse vs `mu_s * normal impulse`), with the slip branch clamping to `mu_d`. (b) "sum both" and (c) "share one budget" have no analogue anywhere.
- **No surveyed engine applies static friction as a position projection**; the nearest analogues are Bullet's opt-in friction anchor and PhysX's "strong friction" error memory, both of which exist precisely to stop the slow slope glide this design fixes directly, and MuJoCo's NoSlip post-pass, whose documentation warns it is "an ad-hoc correction that can occasionally cause instabilities".
- **The reference XPBD kinetic form is explicit and unconditionally stable by construction** ("The explicit form in connection with a Gauss-Seidel update allows us to make this step unconditionally stable!"), which is why the kinetic correction is applied unconditionally here (D12) with a cone projection that cannot reverse the sliding direction (D15).
- **Friction-after-normal is the majority order** (Bullet, PhysX, Rapier, Box2D 3.x) and is what both phases do: the normal multiplier is accumulated before the friction budget is read. Within one position iteration that ordering is now literal — `lagrange.v[cidx] += dlambda_n` is written before the static friction block reads `lambda_n`, so the budget the gate tests includes this iteration's own normal contribution. An earlier revision read the buffer first, which left the first position iteration with a budget of exactly zero; the drift is measured cumulatively from the substep start, so that cost one iteration of friction and no accuracy in the settled case rather than a permanent error, but it also made the effective stick/slip boundary `atan(mu_s * w_t / w_n)` rather than a cleaner threshold. Including this iteration's increment is the more permissive reading, so the bracket moves up by roughly one normal increment and task 4.2's slope bracket has to be re-measured rather than assumed.

## Risks / Trade-offs

- **Stick-slip discontinuity.** With `mu_s > mu_d`, the partition in D4 means a contact either holds or slides at `mu_d`; there is no partial static resistance in the iteration where the drift first exceeds the budget. This is the standard rigid-body stick-slip behaviour, and the static budget is re-granted every substep, so a marginal contact re-tests continuously.
- **Kinetic friction is one substep late relative to the static constraint.** Velocity-level friction changes the velocity the next substep's integration uses; the position phase is what removes the drift within the substep it occurs. This is why both phases exist.
- **The static budget ramps with `lambda_n`.** `lambda_n` accumulates across position iterations, so early iterations have a small budget and the static constraint may skip until the normal constraint has converged. With the configured 50 position iterations this is not a practical limitation; with a very small `num_iter_persubstep` it would be.
- **A newly detected contact's first-substep drift includes the approach displacement.** It is cone-bounded, so the tangential velocity change can never exceed `mu_s` times the normal impulse of the same substep, but it does mean the first substep of an oblique impact applies friction to the approach motion.
- **Extra per-contact state and bindings.** Two `vec4` buffers of `max_contact_point` elements — the tangential accumulators are vectors (D14) — one clear each per substep, one read-modify-write per contributing contact per iteration, and four additional bindings across two shaders (appended, so the consecutive-binding contract holds).
- **The module's exported surface grows by two classes.** `SpatialHashBroadDetector` and `ConvexCollisionDetector` become symbols of `EnginePhysics` (D13), so their constructors and the `BindToScene` / `Record` / `GetResultBuffers` signatures become part of what a module consumer can depend on.
- **The kinematic mirror rule changes what the columns hold.** Any test or tool that submits a kinematic body with a mass and then reads the SoA column will see zero. This is the intended semantics (the column is the solver's view), and the authored values remain in the descriptor and component.
- **Behaviour change for authored static friction.** Scenes that set `static_friction` differently from `dynamic_friction` will slide/stick differently than before, because the coefficient was previously ignored; a body that crept down a slope now holds.
- **Merged-impulse implementation risk.** The normal and tangential impulses must be summed *before* the single per-side scatter; writing the normal first and the tangential second silently discards the first write. The linearity argument (`L` is linear, the scatter is a plain write) is why the merge is exact, and the spec states it explicitly.
- **The invariance tests may not be attainable at the stated tolerances.** The acceptance criteria in `tasks.md` are analytic plus comparative. If a tolerance cannot be met, the implementation stops and hands the measured numbers over for visual judgement rather than loosening the tolerance.
