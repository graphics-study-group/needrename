## Why

The current Jacobi XPBD solver emulates atomic float addition via a `floatBitsToInt`/`atomicCompSwap` spin-loop macro (`ATOMIC_ADD_FLOAT`) on `int[]` SSBOs. This is a well-known workaround for the lack of native `atomicAdd` on float in core GLSL, but it has two drawbacks: (1) the CAS loop can spin under contention, wasting GPU cycles, and (2) the `int`/`float` bit-punning adds noise to every accumulate and apply pass. `GL_EXT_shader_atomic_float` has broad hardware support (Vulkan 1.1+) and provides native `atomicAdd` on `float` SSBO members, eliminating both problems.

## What Changes

- Replace the `ATOMIC_ADD_FLOAT` CAS-loop macro with native `atomicAdd` via `GL_EXT_shader_atomic_float`
- Change Jacobi accumulator buffer types from `int v[]` to `float v[]` in all accumulate and apply shaders
- Remove all `intBitsToFloat`/`floatBitsToInt` bit-punning in apply shaders and velocity accumulate shader
- Update C++ buffer allocation sizes from `sizeof(int)` to `sizeof(float)` (same byte size, semantic change only)
- Add `#extension GL_EXT_shader_atomic_float : enable` to all shaders that perform atomic float operations

## Capabilities

### New Capabilities

- `shader-atomic-float`: Native GPU atomic float addition on SSBOs via `GL_EXT_shader_atomic_float`, replacing the CAS-loop emulation pattern for all XPBD Jacobi accumulator buffers (linear/angular position deltas, linear/angular velocity deltas, delta counts, and contact lagrange multipliers).

### Modified Capabilities

- `xpbd-contact-solve`: The Jacobi contact position and velocity solving requirements change — atomic accumulation uses native `atomicAdd` on `float[]` SSBOs instead of the `ATOMIC_ADD_FLOAT` macro on `int[]` SSBOs. The per-contact lagrange multiplier buffer type changes from `int` to `float`. No behavioral change to the solve algorithm itself.

## Impact

- **Shader files** (7 modified): `xpbd_atomic.glsl`, 4 accumulate shaders (`accumulate_contact_position`, `accumulate_contact_velocity`, `accumulate_fixed_position`, `accumulate_hinge_position`), 2 apply shaders (`apply_body_position_deltas`, `apply_body_velocity_deltas`)
- **C++ files** (1 modified): `XpbdGpuSolver.cpp` — buffer size calculations (same byte count, different type)
- **Extension requirement**: `GL_EXT_shader_atomic_float` (Vulkan 1.1+ with `shaderAtomicFloat` feature)
- **No breaking changes**: All buffers remain the same byte size; the solve algorithm is unchanged
