# physics-app-pause

## Purpose

App-level pause model for `PhysicsApp`: pause is a flag owned by the app, exposed for the caller's loop to decide whether to call `Step`. The scene-level `SetSimulationEnabled` mechanism is no longer toggled by the app after commit.

## Requirements

### Requirement: Pause is an app-level flag

`Pause()` and `Resume()` SHALL only set/clear an app-level paused flag and SHALL NOT touch `PhysicsScene::SetSimulationEnabled` or any other engine-side simulation state.

`IsPaused()` SHALL return the current flag value. `CommitScene` SHALL initialize the flag to paused (true), preserving the previous initial-pause UX.

#### Scenario: Pause only flips the flag

- **WHEN** `Pause()` is called after `CommitScene`
- **THEN** `IsPaused()` returns true
- **AND** `PhysicsScene::IsSimulationEnabled()` is unaffected (remains true as set at commit)

#### Scenario: Resume only flips the flag

- **WHEN** `Resume()` is called while paused
- **THEN** `IsPaused()` returns false

### Requirement: Step runs unconditionally

`Step()` SHALL execute a full physics step regardless of the paused flag. The paused flag SHALL NOT gate, skip, or alter `Step`'s work. The caller's loop decides whether to invoke `Step` by consulting `IsPaused()`.

#### Scenario: Step runs while paused flag is set

- **WHEN** the app's paused flag is set and `Step()` is called
- **THEN** the physics simulation advances by one step (bodies fall / integrate normally)

### Requirement: CommitScene enables simulation once and never toggles it

`CommitScene` SHALL call `PhysicsScene::SetSimulationEnabled(true)` exactly once so the solver records dispatches, and SHALL NOT call `SetSimulationEnabled` again afterwards. The engine-level enable/disable mechanism remains available to other hosts (e.g. the editor) and is out of scope for the app.

#### Scenario: Simulation is enabled after commit

- **WHEN** `CommitScene` returns
- **THEN** the app's `PhysicsScene::IsSimulationEnabled()` returns true

#### Scenario: No further toggling from the app

- **WHEN** `Pause()` and `Resume()` are called in any order after commit
- **THEN** `PhysicsScene::IsSimulationEnabled()` stays true throughout

### Requirement: SPACE toggle controls the app pause flag

In `Windowed` mode, the built-in SPACE key handling in `RenderNextFrame` SHALL keep toggling the app pause flag via `Pause()` / `Resume()`; the toggle SHALL NOT affect scene-level simulation enablement.

#### Scenario: SPACE toggles pause in windowed mode

- **WHEN** the app runs windowed, is unpaused, and the SPACE press event is processed by `RenderNextFrame`
- **THEN** `IsPaused()` becomes true
- **AND** the next SPACE press makes it false again

### Requirement: Caller loop drives stepping via the flag

The app's own driver code (windowed test loop) SHALL call `Step()` only when `IsPaused()` is false, and SHALL keep calling `RenderNextFrame()` regardless, so rendering and input continue while paused.

#### Scenario: Windowed test loop honors the flag

- **WHEN** the windowed test main loop runs while the pause flag is set
- **THEN** `Step()` is not called during that frame and `RenderNextFrame()` still runs

### Requirement: CommitScene seeds initial model matrices

In rendering modes (`Windowed`, `Offscreen`), `CommitScene` SHALL run a one-time solver pass while scene simulation is still disabled (before `SetSimulationEnabled(true)`) so that only the solver's model-matrix computation executes and the initial model matrices are written from the `FlushPhysics`-seeded poses. Bodies SHALL NOT advance during this pass.

Because the solver's model-matrix dispatch is the only writer of the GPU model-matrices buffer and `FlushPhysics` does not seed it, without this pass the renderer would read a zeroed buffer while paused and every body would be invisible (only the skybox would show).

#### Scenario: Bodies are visible while paused

- **WHEN** `CommitScene` returns in `Windowed` mode and `RenderNextFrame` runs while paused (before any `Step`)
- **THEN** bodies render at their initial pose (the frame shows more than the skybox)
- **AND** the bodies' state has not advanced (no physics integration ran)

#### Scenario: Seed pass does not advance simulation

- **WHEN** `CommitScene` returns and the first `Step` is then called
- **THEN** the simulation advances from the initial poses (the seed pass did not integrate)

### Requirement: Paused frames keep the renderer drained

While the pause flag is set, `RenderNextFrame` SHALL drain the GPU (device idle wait) at the end of each rendered frame, because the caller's loop does not call `Step()` (whose device waits previously throttled the renderer). This SHALL prevent swapchain semaphores from being re-signaled before their previous present operation completes.

#### Scenario: Paused loop is free of swapchain validation errors

- **WHEN** the windowed app runs paused for many frames without any `Step`
- **THEN** no swapchain semaphore-reuse validation errors occur (e.g. `VUID-vkQueueSubmit2-semaphore-03868`)
