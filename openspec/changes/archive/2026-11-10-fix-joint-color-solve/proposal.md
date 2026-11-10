## Why

Joint constraints are solved through the same scatter -> segmented reduction -> per-body apply pipeline as contacts, and that pipeline averages each body's accumulated partials by the total contribution count. A body's joint correction is therefore divided by however many constraint slots touch it, contacts included, so a joint's effective stiffness depends on its body's graph connectivity, the two ends of one joint can be scaled by different factors (which does not conserve momentum), and a joint competing with a contact manifold nearly disappears. The per-color rewrite now in the working tree removes that averaging, but pays four dispatches per color — twenty per position iteration for an 18-body quadruped — to move at most five joints at a time, and it still scans every joint once per color.

## What Changes

- Joint constraints leave the contact pipeline entirely: a joint reads its two bodies and writes their corrected poses directly. There is no per-body partial set, contribution count, entry scratch, `SumByKey` reduction or apply pass for joints.
- Joint corrections are applied at full strength. The division by contribution count is removed for joints, so a joint's correction no longer depends on how many contacts or other joints touch its bodies.
- Joints are solved one color at a time (a colored Gauss-Seidel sweep), and a color's joints never share a body. Colors are published as a **bucket index list**: an array of joint slot ids ordered by color, plus per-color ranges. Joint descriptors are never reordered, so slot ids stay stable for the constraint components that own them.
- Each color is dispatched over only that color's joints (`ceil(n_c / workgroup_size)` workgroups). This removes the fixed `ceil(joint_count / workgroup_size)` dispatch per color whose lanes mostly idle.
- A single-workgroup fast path collapses the whole sweep into one dispatch when every color fits in one workgroup, synchronising colors with a buffer memory barrier plus a workgroup barrier instead of a global pipeline barrier.
- Coloring is recomputed only when joint topology changes, excludes disabled joints, and does not treat a shared kinematic body as a conflict.
- **BREAKING** (solver internals): the hinge and fixed entry lists, their per-iteration entry scratch, their push-counted clear, their entry counts and their per-body partial buffers are removed, along with `entries/hinge_entries.comp`, `entries/fixed_entries.comp` and `clear_entry_values_push.comp`.
- The joint descriptors revert to the documented 80-byte layout with `_pad`; no color field is stored in GPU data.
- The actuator stand test runs headless for its finite frame count, so its ctest label changes from `windowed` to `headless`.

## Capabilities

### New Capabilities

- `xpbd-joint-color-solve`: how hinge and fixed joints are colored, bucketed and solved — direct pose write per color, color disjointness, the bucket index list, per-color dispatch bounds, the single-workgroup fast path, and when coloring is rebuilt.

### Modified Capabilities

- `xpbd-contact-solve`: joints are removed from the Jacobi contact pipeline. The contact position solve, entry construction, accumulator clearing, per-substep entry sorting and multiplier-lifetime requirements narrow to the contact group, and the per-body apply no longer merges joint partials.
- `physics-gpu-shaders`: the solver's required shader set drops the hinge/fixed entry passes and the push-counted entry clear, and the joint accumulate shaders become direct-write passes.
- `physics-push-constants`: the hinge/fixed entry-count push constants, the push-counted entry clear form, and the joint entry shaders' body-count push value cease to exist.

## Impact

- **CPU**: `PhysicsScene` (color computation, bucket construction, per-color ranges, rebuild trigger), `PhysicsGpuBuffers` (bucket and range buffers, color counts), `PhysicsDescriptors.h` (`color` field reverted to `_pad`).
- **Solver**: `XPBDGpuSolver` (position-iteration recording, buffer sizing, kernel set, push-constant blocks).
- **Shaders**: `accumulate_hinge_position.comp` and `accumulate_fixed_position.comp` become read-modify-write direct-write passes; `apply_body_position_deltas.comp` loses its hinge/fixed bindings; `entries/hinge_entries.comp`, `entries/fixed_entries.comp` and `clear_entry_values_push.comp` are deleted; a joint color-sweep shader is added.
- **Tests**: `test/app/physics/physics_app_actuator_stand_test.cpp` and its `CMakeLists.txt` entry (frame count and label).
- **No public API change**: `PhysicsApp`'s `JointId`, actuator and readback paths are untouched because joint slot ids stay stable.
