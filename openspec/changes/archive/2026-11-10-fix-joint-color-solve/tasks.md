## 1. Direct pose write per color

- [x] 1.1 Add a shared `solver/XPBDSolver/common/xpbd_joint_solve.glsl` with one entry point per joint type that reads a joint by slot id, evaluates both of its constraints and writes both bodies' pose; verify the SPIR-V builds for every shader that includes it
- [x] 1.2 Convert `accumulate_hinge_position.comp` to one color of direct pose writes using the shared include; verify it still compiles and the A1 actuator stand test reaches its upright / settle / joint-error thresholds
- [x] 1.3 Convert `accumulate_fixed_position.comp` the same way; verify the A1 actuator stand test still passes
- [x] 1.4 Record one joint accumulate dispatch per color and delete the per-color clear, reduction and apply for joints; delete `apply_joint_position_deltas.comp`; verify a step's position-iteration joint dispatches drop from four per color to one per color (add a temporary log if needed) and the stand test passes
- [x] 1.5 Remove the hinge/fixed bindings and the per-type partial merging from `apply_body_position_deltas.comp`, leaving the contact group alone; verify the pyramid stability test and the friction / joint-torque / reset tests pass
- [x] 1.6 Confirm the correction is no longer diluted: a body that carries both a joint and contacts receives the joint's full correction; verify by comparing the A1 stand's settled joint error against the pre-change value and by inspecting one pixel/pose trace, and record the numbers

## 2. Color buckets

- [x] 2.1 Rewrite the scene-side coloring to run over alive joints only and to ignore kinematic bodies as conflicts, and produce a per-type bucket list plus per-color ranges; verify with a unit test asserting the A1 quadruped yields four hinge colors and one fixed color, and that a disabled joint consumes no color
- [x] 2.2 Rebuild the color data only when a joint is allocated, unregistered, re-submitted or a body's kinematic flag changes, reusing scratch storage; verify with a counter/log that an idle frame performs no rebuild and no allocation
- [x] 2.3 Publish the bucket list and per-color range buffers through `PhysicsGpuBuffers` and record the color counts the solver needs; verify the buffers are created and sized on the first step and reused while the topology is unchanged
- [x] 2.4 Switch the per-color joint dispatch to `sorted_ids[offset + i]` with the color's count and offset in the push block, and drop the per-lane color compare; verify the head of every color's slice carries that color and every alive joint is visited exactly once per iteration
- [x] 2.5 Delete the joint entry machinery: `entries/hinge_entries.comp`, `entries/fixed_entries.comp`, `clear_entry_values_push.comp`, the hinge/fixed `keys` / `payload` / `values` / `out` buffers, the joint entry counts and the joint reduction recording; verify no source, CMake or shader reference to a deleted file remains
- [x] 2.6 Revert `HingeJointComDescriptor` and `FixedJointComDescriptor` to the documented `_pad` layout and remove the color writes from the descriptor upload; verify `sizeof` is 80 bytes via the existing `static_assert` and the shaders' struct declarations match
- [x] 2.7 Add a bucket-builder unit test over many independent copies of one robot's joint graph (for example 1024 x 17 joints) asserting the color count stays at the single-robot value and that every color's slice is contiguous and reaches at least a full workgroup; verify the test fails if the secondary ordering by connected component is introduced

## 3. Single-workgroup sweep

- [x] 3.1 Add `joint_color_sweep.comp` that walks the fixed colors then the hinge colors in one workgroup, using `memoryBarrierBuffer(); barrier();` between colors and a guarded work block that never returns before the final barrier; verify it compiles and a single-workgroup scene produces the same poses as the per-color path
- [x] 3.2 Select the mode on the CPU: sweep when every color of a type fits in one workgroup, per-color dispatch otherwise; verify the A1 stand uses the sweep (one joint dispatch per iteration) and a scene whose color exceeds a workgroup falls back
- [x] 3.3 Add an equivalence test that runs the same scene through both modes and compares every body's position and orientation within a frame-identical tolerance; verify it passes
- [x] 3.4 Measure the A1 stand's step time in both modes and pick the workgroup size and threshold from the result; verify the recorded numbers land at or under the 200 Hz step budget of 5 ms

## 4. Validation and housekeeping

- [x] 4.1 Change the actuator stand test's finite mode to run headless and its ctest label from `windowed` to `headless`; verify `ctest -L headless` includes it and `ctest -L windowed` does not
- [x] 4.2 Run the full physics test suite (`ctest --preset msvc-debug`) and verify every physics app test passes, including the actuator stand, pyramid stability, friction, joint torque and reset tests
- [x] 4.3 Verify the shipped shader set matches the specification: no joint entry shader, no push-counted clear, and a pipeline for each of the two per-color joint shaders, the sweep and the two joint lagrange clears
- [x] 4.4 Remove stale comments and documentation that still describe joint scatter/reduce/apply or a per-joint color field; verify `engine/Physics/` and the physics shaders contain no reference to a joint entry list
