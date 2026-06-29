## 1. Common header update

- [x] 1.1 Remove `ATOMIC_ADD_FLOAT` CAS-loop macro from `engine/Physics/shader/solver/XPBDSolver/common/xpbd_atomic.glsl`, replace file header comment to document that `GL_EXT_shader_atomic_float` native `atomicAdd` is now used directly

## 2. Accumulate shaders — float buffer types and native atomicAdd

- [x] 2.1 Update `accumulate_contact_position.comp`: add `#extension GL_EXT_shader_atomic_float : enable`, change `LinearPositionDeltaI { int v[]; }` → `LinearPositionDelta { float v[]; }`, same for `AngularPositionDeltaI`→`AngularPositionDelta`, `PositionDeltaCount { int v[]; }`→`{ float v[]; }`, `ContactLagrange { int v[]; }`→`{ float v[]; }`, replace `ATOMIC_ADD_FLOAT` calls with `atomicAdd`, change `ATOMIC_ADD_FLOAT(delta_count.v[...], 1.0)` to `atomicAdd(delta_count.v[...], 1.0)`
- [x] 2.2 Update `accumulate_contact_velocity.comp`: add extension, change `LinearVelocityDeltaI { int v[]; }`→`LinearVelocityDelta { float v[]; }`, same for `AngularVelocityDeltaI`→`AngularVelocityDelta`, `VelocityDeltaCount { int v[]; }`→`{ float v[]; }`, change `ContactLagrange { int v[]; }`→`{ float v[]; }` (readonly), replace `ATOMIC_ADD_FLOAT` with `atomicAdd`, replace `intBitsToFloat(lagrange.v[cidx])` with `lagrange.v[cidx]`
- [x] 2.3 Update `accumulate_fixed_position.comp`: add extension, change `LinearPositionDeltaI { int v[]; }`→`LinearPositionDelta { float v[]; }`, same for `AngularPositionDeltaI` and `PositionDeltaCount`, replace `ATOMIC_ADD_FLOAT` with `atomicAdd`
- [x] 2.4 Update `accumulate_hinge_position.comp`: add extension, same buffer type changes as fixed position, replace `ATOMIC_ADD_FLOAT` with `atomicAdd`

## 3. Apply shaders — remove intBitsToFloat, use float types

- [x] 3.1 Update `apply_body_position_deltas.comp`: change `LinearPositionDeltaI { int v[]; }`→`LinearPositionDelta { float v[]; }`, `AngularPositionDeltaI { int v[]; }`→`AngularPositionDelta { float v[]; }`, `PositionDeltaCount { int v[]; }`→`{ float v[]; }`, remove all `intBitsToFloat()` calls (6 per body), read accumulators as direct float values, reset to `0.0` instead of `0`
- [x] 3.2 Update `apply_body_velocity_deltas.comp`: change `LinearVelocityDeltaI { int v[]; }`→`LinearVelocityDelta { float v[]; }`, `AngularVelocityDeltaI { int v[]; }`→`AngularVelocityDelta { float v[]; }`, `VelocityDeltaCount { int v[]; }`→`{ float v[]; }`, remove all `intBitsToFloat()` calls, read as direct floats, reset to `0.0`

## 4. C++ buffer sizing and binding name update

- [x] 4.1 In `XpbdGpuSolver.cpp`, change `body_int3` calculation from `body_count * 3 * sizeof(int)` to `body_count * 3 * sizeof(float)`
- [x] 4.2 Change `body_int1` from `body_count * sizeof(int)` to `body_count * sizeof(float)`
- [x] 4.3 Change `contact_lagrange_bytes` from `max_contacts * sizeof(int)` to `max_contacts * sizeof(float)`
- [x] 4.4 Update `srb.BindBuffer` calls: rename `"LinearPositionDeltaI"`→`"LinearPositionDelta"`, `"AngularPositionDeltaI"`→`"AngularPositionDelta"`, `"LinearVelocityDeltaI"`→`"LinearVelocityDelta"`, `"AngularVelocityDeltaI"`→`"AngularVelocityDelta"` (found in all accumulate and apply pass binding sections)

## 5. Build and verify

- [x] 5.1 Build the engine (`cmake --build`) to confirm all shaders compile to SPIR-V without GLSL errors
- [x] 5.2 Run existing physics tests/examples to verify no behavioral regression
