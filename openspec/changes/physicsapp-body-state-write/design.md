# Design: PhysicsApp per-body state writes

## Context

`PhysicsApp` already exposes a complete readback path: at `CommitScene` it maps `BodyId` → rigid-body slot, allocates four persistent readback staging buffers, and `Step` records `copyBuffer` of the body SoA into them inside the dedicated physics command buffer (fence-waited before return). The whole app is single-threaded, and `Step` is wrapped in `WaitForIdle` before/after, so CPU access to staging is always safe.

What is missing is the symmetric **write path**: external drivers (test harnesses now, RL/Python control loops later) need to override body state and inject forces before each step.

The engine's own physics path (editor) submits physics on the rotating multi-frame render command buffers with no `waitIdle` discipline. A CPU write to staging there races with unexecuted copies from earlier frames (a `copyBuffer` reads its source at CB execution time), so engine-level support would require per-frame-in-flight staging versions. No engine consumer needs runtime writes today.

## Goals / Non-Goals

**Goals:**

- Drive-phase API to set exactly six per-body fields (position, rotation, linear/angular velocity, external force/torque) before `Step`.
- Values uploaded to the GPU physics buffers before the solver dispatch of the same step.
- Simple, uniform semantics: direct overwrite, persists until overwritten; forces cleared only by the caller.
- Pause becomes an app-level flag; `Step` runs unconditionally; the caller's loop decides via `IsPaused()`.
- Zero engine-layer changes.

**Non-Goals:**

- No batch/SoA write API (deferred until GPU-array exposure to Python materializes).
- No editor-path runtime writes (needs per-frame-in-flight staging; deferred).
- No `BodyId` ↔ slot unification (the observed equality is coincidental; the mapping stays).
- No engine-side force clearing, no solver changes, no `PhysicsScene` API changes.

## Decisions

### D1: Write path lives entirely in `PhysicsApp` (engine untouched)

The app's `Step` waits for the device before and after physics work, so a single set of staging buffers written between steps can never race an in-flight command buffer. This is the same discipline the existing readback path relies on.

Alternatives considered:

- **Engine-level write path on `PhysicsScene` + automatic upload in `PhysicsSystem::GPUStep`** — reusable, but the editor's 3-frame rotating submission means staging must be versioned per frame in flight or writes must be deferred/buffered ("cache until the right time"), which the user flagged as risky and unneeded today. Rejected.
- **Host-visible device buffers written directly by CPU mapping** — would avoid staging copies, but position/velocity/force buffers are read by compute every substep; host-visible memory can hurt discrete-GPU bandwidth, and it breaks symmetry with the readback staging decision. Rejected.

### D2: Single-entry API with a field enum and unified `glm::vec4`

`SetBodyValue(BodyId, BodyField, glm::vec4)` with `BodyField ∈ {Position, Rotation, LinearVelocity, AngularVelocity, ExternalForce, ExternalTorque}`.

Alternatives considered:

- **Five typed setter methods + a quaternion setter** — self-documenting but splits the API across 6 methods and complicates a future C binding. Rejected.
- **Enum-keyed overloads (`SetBodyValue(id, Position, vec3)` / `(id, Rotation, quat)`)** — C++-friendly but cannot map to a single C entry point later. Rejected.
- **Value union/variant** — heavier than needed for a pass-through write API. Rejected.

`vec4` is the exact GPU storage layout (rotation = quat xyzw; other fields xyz with w=0), so the write path is a straight memcpy of the value into staging.

### D3: Direct-overwrite semantics; no solver clearing

A set value replaces the GPU slot at the next step and persists until overwritten. The solver does not clear force/torque; their lifetime is caller-managed. This aligns with `RigidBodyComponent::m_external_force/m_external_torque`, which are constant configuration values applied every substep today — the app-level write behaves the same way, so there is one force model instead of two.

Alternatives considered:

- **Per-frame clear in the solver** (the original request) — a dedicated clear shader dispatched after the substep loop. But unconditional clearing would silently break engine constant-force semantics (component fields are uploaded once at build time and never re-submitted), and a scene-level opt-in flag would fork semantics between hosts. Rejected in favor of caller-managed lifetime.
- **Clear inside `integrate_forces`** — forces would only act on the first substep (default `num_substep_perstep=2` halves the effective impulse); also requires binding changes. Rejected.

Consequence accepted: a forgotten force keeps pushing. Tests will assert the persistence semantics explicitly.

### D4: Persistent per-field staging + whole-field dirty uploads in the Step CB

At `CommitScene`, next to the readback staging, allocate six persistent `StagingToDevice` buffers sized `slot_count × sizeof(vec4)` (one per field). `SetBodyValue` maps `BodyId → slot` via the existing `body_to_slot` table, writes the vec4 into staging, and sets `any_dirty[field]`.

`Step`'s command buffer records, **before** `GPUStep`: for each dirty field, one `copyBuffer(staging → GPU buffer)` covering the whole buffer. Dirty flags are cleared after the fence wait (post-submit), mirroring the readback path's ordering.

Alternatives considered:

- **Per-slot dirty ranges with offset copies** — only worth it at much larger body counts; 128 slots × 16 B = 2 KB per field makes whole-field copies negligible. Rejected as premature.
- **Unconditional full upload every step** — would re-apply stale position/velocity values each frame ("teleport-back" bug). Rejected; dirty tracking is a correctness requirement, not an optimization.
- **Upload via `SubmissionHelper` / FrameManager** — contradicts the existing decision that the app's physics data path does not use FrameManager callbacks and relies on device waits. Rejected.

The structure deliberately leaves room for the deferred batch API: it is a loop over `(id, value)` pairs plus one flag per field.

### D5: Pause is an app-level flag; Step is unconditional

`Pause()`/`Resume()` only flip `impl.paused`. `Step()` always performs a full step. The caller's loop calls `Step()` only when `IsPaused()` is false (the windowed test loop is updated accordingly); `RenderNextFrame` keeps running and its built-in SPACE handling keeps toggling the flag.

Because `PhysicsScene::m_simulation_enabled` defaults to false, `CommitScene` calls `SetSimulationEnabled(true)` exactly once and the app never toggles it again — otherwise the solver's `IsSimulationEnabled()` gate would silently record nothing. The engine-level enable/disable API stays untouched for the editor.

Alternatives considered:

- **Keep the paused-state no-op inside `Step`** — hides control from the caller and keeps two pause concepts in one object. Rejected per user direction.
- **Change `PhysicsScene`'s default to enabled** — engine behavior change benefiting only the app. Rejected.

### D6: BodyId remains the app registry index

`BodyId` is the index into `SceneBuilder::m_bodies` (assigned at Add time, before slots exist). The rigid-body slot is allocated during `FlushPhysics` and resolved via `FindRigidBodyByObjectHandle`. The currently observed `BodyId == slot` equality is incidental (one rigid body per app body, same creation order) and is not guaranteed. The write path keeps using `body_to_slot`.

### D7: CommitScene seeds initial model matrices with a no-advance solver pass

The GPU `model_matrices` buffer has exactly one writer: the solver's `GPUStep` model-matrix dispatch (`PhysicsScene::RefreshGpuBuffers` allocates the buffer but never uploads initial data). Before this change the windowed loop called `Step()` every frame, and that dispatch runs regardless of `IsSimulationEnabled()`, so bodies were visible even while "paused". The new pause model skips `Step()` while paused, leaving the buffer zeroed → every body invisible (skybox only).

`CommitScene` now runs a one-time `PreGPUStep` + `GPUStep` + `PostGPUStep` on a dedicated command buffer (fence-waited, same pattern as the readback seed in `BuildPhysicsReadback`) while scene simulation is still disabled — the enable call comes at the end of `CommitScene` — so the solver executes only the model-matrix dispatch and writes initial matrices from the `FlushPhysics`-seeded poses. Bodies do not advance; a later `Step` re-runs `PreGPUStep` idempotently. Gated to rendering modes (`!= PhysicsOnly`).

Alternatives considered:

- **Seed the buffer in `PhysicsScene::RefreshGpuBuffers` (engine change)** — CPU-side initial matrices would duplicate the shader's model-matrix math and diverge from solver semantics; violates the zero-engine-changes goal. Rejected.
- **Have the test loop call `Step()` once after `CommitScene`** — with simulation enabled that advances bodies by one step before the first frame and breaks the "paused at initial pose" UX; also leaks app behavior into every driver. Rejected.

### D8: Paused rendering drains the device each frame

`Step()` calls `renderer->WaitForIdle()` (device `waitIdle`) before and after physics work; the old always-Step loop was therefore fully throttled and every present completed before the next frame. The new paused loop only calls `RenderNextFrame()` and runs uncapped (mailbox present mode does not block the acquire), so the present queue lags the graphics queue and `FrameManager` re-signals its per-FIF `frame_completed_semaphore` before the previous present that waits on it has completed — `VUID-vkQueueSubmit2-semaphore-03868` ("Swapchain image N was presented but was not re-acquired").

`RenderNextFrame` now calls `renderer->WaitForIdle()` at the end of each frame while the pause flag is set, restoring the old pacing exactly where `Step` is absent. While running, `Step`'s own waits already cover this.

Note: the underlying `FrameManager` pacing issue (binary present-wait semaphore indexed by FIF rather than by swapchain image, no present-completion wait) is a pre-existing engine bug exposed by the uncapped loop, not introduced here. A proper engine fix (per-image present semaphores, a present-completion fence, or `VK_KHR_swapchain_maintenance1`) is deferred to a follow-up change; the app-level drain is the in-scope mitigation.

Alternatives considered:

- **Skip rendering entirely while paused** — violates the requirement that rendering and input continue while paused (camera controls, SPACE toggle). Rejected.
- **Wait only on the last frame fence (`WaitForFrameCompletion`)** — the fence is signaled when the graphics batch completes, which precedes present completion; the present queue can still lag. Rejected as insufficient.

## Risks / Trade-offs

- **[Forgotten force zeroing keeps pushing a body]** → Caller-managed lifetime is the documented contract; identical to component constant-force semantics; tests assert persistence.
- **[Staging written while a CB is in flight]** → Safe only because `Step` waits on the fence before returning and the app is single-threaded. The contract "call `SetBodyValue` between `Step` calls" is documented in the API doc.
- **[vec4 API loses field/type safety]** → Compiler cannot catch passing a quat to `Position`. Mitigated by clear per-field docs and by the fact that the value layout mirrors the GPU buffer exactly.
- **[Kinematic bodies ignore set forces]** → `integrate_forces` zeroes acceleration for kinematic bodies, so force writes have no visible effect on them (position/velocity writes still apply). Documented behavior.
- **[Unnormalized rotation input]** → The solver normalizes the quaternion after integration; garbage input (zero quat) can still corrupt the body. The app normalizes on write to be safe.
- **[Paused driver that ignores `IsPaused()` still advances physics]** → By design; the flag is informational for the loop.
- **[Zeroed model matrices while paused hide all bodies]** → `model_matrices` is allocated but never seeded by `FlushPhysics`, and its only writer is the solver's `GPUStep` dispatch, which the paused loop never reaches. Addressed by D7 (commit-time seed pass).
- **[Uncapped paused loop races the present queue]** → `Step`'s device waits previously throttled every frame; without them the renderer's per-FIF present-wait semaphore is re-signaled before its previous present completes. Addressed by D8 (paused drain); the engine-side pacing bug is tracked as a follow-up.

## Open Questions

- None blocking. The batch write API shape (id/value pairs vs. mutable staging views) is deferred and will be designed when the Python GPU-array exposure work starts.
