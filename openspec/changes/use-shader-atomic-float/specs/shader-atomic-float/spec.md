## ADDED Requirements

### Requirement: GL_EXT_shader_atomic_float extension enabled

All XPBD compute shaders that perform atomic float addition on SSBO members SHALL declare `#extension GL_EXT_shader_atomic_float : enable`. This extension provides native `atomicAdd`, `atomicExchange`, `atomicLoad`, and `atomicStore` built-in functions for `float` and `double` types on buffer and shared variables.

The extension SHALL be enabled in any shader that calls `atomicAdd` with a `float` argument. Shaders that only use integer atomics (`atomicAdd` on `uint`) SHALL NOT require this extension.

#### Scenario: Accumulate shaders compile with the extension

- **WHEN** any XPBD accumulate shader is compiled by `glslangValidator`
- **THEN** the shader source contains `#extension GL_EXT_shader_atomic_float : enable`
- **AND** compilation succeeds without extension-related errors

#### Scenario: Apply shaders do not need the extension

- **WHEN** an apply shader (which only reads float values and writes ordinary assignments) is compiled
- **THEN** it compiles successfully without `GL_EXT_shader_atomic_float`

### Requirement: Native atomicAdd on float SSBO members

The system SHALL use the native `atomicAdd` built-in provided by `GL_EXT_shader_atomic_float` for all Jacobi accumulation operations on float values. The basic two-argument form `atomicAdd(mem_ref, val)` SHALL be used; explicit memory scope and semantics qualifiers SHALL NOT be required for SSBO operations.

The CAS-loop macro `ATOMIC_ADD_FLOAT` that operates on `int` memory via `floatBitsToInt`/`atomicCompSwap` SHALL be removed.

#### Scenario: Linear position delta accumulation uses native float atomicAdd

- **WHEN** a contact accumulation shader writes a linear position delta for body A
- **THEN** the shader calls `atomicAdd(lin_delta.v[body_a * 3u + 0u], lin_d.x)`
- **AND** `lin_delta` is declared as `buffer LinearPositionDelta { float v[]; }` (not `int v[]`)
- **AND** no `floatBitsToInt`/`atomicCompSwap` loop is present

#### Scenario: Angular velocity delta accumulation uses native float atomicAdd

- **WHEN** a velocity accumulation shader writes an angular velocity delta for body B
- **THEN** the shader calls `atomicAdd(ang_vel_delta.v[body_b * 3u + 1u], ad.y)`
- **AND** `ang_vel_delta` is declared as a `float` buffer

#### Scenario: Delta count uses native atomic float increment

- **WHEN** a body receives a position or velocity delta
- **THEN** the shader calls `atomicAdd(delta_count.v[body_idx], 1.0)`
- **AND** `delta_count` is declared as `buffer PositionDeltaCount { float v[]; }`

### Requirement: Jacobi accumulator buffers are float-typed

All XPBD Jacobi accumulator SSBOs SHALL be declared with `float` element type in shader layout declarations, replacing the previous `int` element type. This applies to:

- `LinearPositionDeltaI` / `LinearPositionDelta` — linear position deltas (3 floats per body)
- `AngularPositionDeltaI` / `AngularPositionDelta` — angular position deltas (3 floats per body)
- `PositionDeltaCount` — per-body delta count (1 float per body)
- `LinearVelocityDeltaI` / `LinearVelocityDelta` — linear velocity deltas (3 floats per body)
- `AngularVelocityDeltaI` / `AngularVelocityDelta` — angular velocity deltas (3 floats per body)
- `VelocityDeltaCount` — per-body velocity delta count (1 float per body)

The per-contact Lagrange multiplier buffer `ContactLagrange` SHALL also be changed from `int v[]` to `float v[]`.

#### Scenario: Accumulate shader buffer declarations use float

- **WHEN** an accumulate shader (contact, fixed, or hinge position) declares its accumulator buffer bindings
- **THEN** `LinearPositionDeltaI` is declared as `{ float v[]; }` (not `{ int v[]; }`)
- **AND** `AngularPositionDeltaI` is declared as `{ float v[]; }` (not `{ int v[]; }`)
- **AND** `PositionDeltaCount` is declared as `{ float v[]; }` (not `{ int v[]; }`)

#### Scenario: Apply shader reads float accumulators without bit-punning

- **WHEN** the body position delta apply shader reads accumulated deltas
- **THEN** it reads them as `linear_delta_i.v[idx * 3u + 0u]` directly (a float value)
- **AND** no `intBitsToFloat()` call is present in the shader
- **AND** the accumulator is reset to `0.0` (not `0`)

### Requirement: CPU-side buffer size reflects float element type

The C++ buffer allocation in `XpbdGpuSolver.cpp` SHALL use `sizeof(float)` instead of `sizeof(int)` when computing byte sizes for Jacobi accumulator buffers. Since `sizeof(float) == sizeof(int) == 4` on all supported platforms, the actual byte sizes SHALL remain unchanged; this is a semantic correctness change.

#### Scenario: Buffer allocation size unchanged after type change

- **WHEN** `EnsureBuffer` is called for `gpu_linear_position_delta` with `body_count` bodies
- **THEN** the byte size is `body_count * 3 * sizeof(float)` (was `sizeof(int)`)
- **AND** the resulting allocation is exactly the same number of bytes as before
