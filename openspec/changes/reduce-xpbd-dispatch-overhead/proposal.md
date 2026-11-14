## Why

The reference PhysicsApp issues about 1220 XPBD dispatches per step, and GPU measurements show that both backends are dominated by command processing rather than shader arithmetic. The contact scratch clear is removable, and the already validated single-workgroup joint sweep is currently deferred on WebGPU because it binds 18 storage buffers and uses a barrier form Tint cannot translate; restoring it portably reduces dispatch overhead while bringing the browser path back to the existing solver design.

## What Changes

- Initialize each contact entry slot at the start of its position or velocity accumulate invocation, preserve the existing guards and scatter behavior, and remove the standalone per-iteration `ClearEntryValues` shader and dispatches.
- Keep the existing small-scene sweep and large-color per-color strategies, color order, substep/iteration counts, and constraint math unchanged; ordinary floating-point variation is acceptable, but reduced solve quality is not.
- Replace four fixed/hinge GPU color arrays with shared `JointColorIds` and `JointColorOffsets` tables populated by CPU-side concatenation whenever joint colors are rebuilt.
- Reduce the joint sweep from 18 storage buffers to 16, derive hinge table bases from existing color metadata without enlarging its 32-byte constant block, and reuse the shared IDs table in per-color fallback dispatches.
- Express sweep synchronization with workgroup-scope control barriers that bake to WGSL `storageBarrier` and `workgroupBarrier`, and restore the sweep to normal SPIR-V/WGSL builds.
- Keep 16 storage buffers as a hard browser constraint for every enabled physics shader; do not request a higher device limit.
- Restore portable Vulkan/desktop-WebGPU sweep equivalence coverage and add a finite browser joint correctness smoke with explicit pass/fail evidence.
- Target approximately 740 dispatches per step in the reference scene, down from about 1220, without using an absolute frame-time threshold as a portable acceptance gate.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `xpbd-contact-solve`: Per-iteration contact scratch is initialized by each accumulate invocation rather than by a standalone counted-clear dispatch; non-contributing and skipped slots retain zero contribution semantics.
- `xpbd-joint-color-solve`: Fixed and hinge color metadata is published in shared GPU tables consumed by both sweep and per-color modes, and the single-workgroup sweep becomes browser-compatible without changing mode selection or solve order.
- `physics-push-constants`: Define shared-table element indexing and base derivation while preserving the existing per-color and sweep constant-block layouts.
- `physics-gpu-shaders`: Remove the standalone contact scratch-clear artifact, restore the joint sweep artifact, and require browser-compatible storage synchronization and binding counts.

## Impact

- Contact solve and shader initialization: `engine/Physics/Solver/XPBDGpuSolver.cpp`, `accumulate_contact_position.comp`, `accumulate_contact_velocity.comp`, `xpbd_scatter.glsl`, and the removed `clear_entry_values.comp` path.
- Joint color ownership and upload: `engine/Physics/PhysicsScene.h`, `engine/Physics/PhysicsScene.cpp`, and the GPU buffer view consumed by the solver.
- Joint shaders and build registration: `joint_color_sweep.comp`, the unchanged per-color accumulate shaders, and `engine/Physics/CMakeLists.txt`.
- Existing headless physics tests, the deferred sweep equivalence test, actuator regression coverage, and a finite browser joint smoke.
- No higher WebGPU device limits, new third-party dependencies, RHI API changes, reduction/sort redesign, or permanent performance-analysis framework are introduced.
