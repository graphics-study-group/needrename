## Context

The XPBD solver currently uses a CAS-loop macro `ATOMIC_ADD_FLOAT` in `xpbd_atomic.glsl` to emulate float atomic addition on `int[]` SSBOs. Under contention (many threads accumulating onto the same body), the CAS loop spins, wasting ALU cycles. `GL_EXT_shader_atomic_float` provides native `atomicAdd` on float SSBO members and is widely supported on Vulkan 1.1+ hardware.

The change is mechanical: 7 shader files and 1 C++ file. Buffer byte sizes are unchanged (`sizeof(float) == sizeof(int)`), so there is no data migration, no layout changes, and no API breakage.

## Goals / Non-Goals

**Goals:**
- Replace CAS-loop float atomic emulation with native `atomicAdd` on float SSBOs
- Remove all `intBitsToFloat`/`floatBitsToInt` boilerplate in accumulate and apply shaders
- Keep the same solve algorithm — only the atomic mechanism changes
- Maintain backward compatibility with the existing SPIR-V build pipeline (CMake `glslangValidator` with `#extension` support)

**Non-Goals:**
- Change the XPBD algorithm or constraint formulation
- Change buffer sizes, descriptor set layouts, or binding indices
- Switch to the Vulkan memory model (`GL_KHR_memory_scope_semantics`) — we use the basic `atomicAdd` form without explicit scope/semantics
- Change integer-only atomic buffers (`collision_count`, radix sort histograms, spatial hash counters) — those already use native `atomicAdd` on `uint`

## Decisions

### D1: Remove the macro, use `atomicAdd` directly

**Decision**: Delete `ATOMIC_ADD_FLOAT` from `xpbd_atomic.glsl` (and possibly the entire file). Use `atomicAdd` directly in accumulate shaders.

**Rationale**: With the extension, `atomicAdd(float_mem, float_val)` has the same signature pattern as the macro. Wrapping it adds no value — every GLSL programmer knows what `atomicAdd` means, but `ATOMIC_ADD_FLOAT` requires looking up a project-specific definition.

**Alternative considered**: Keep a thin `#define ATOMIC_ADD_FLOAT atomicAdd` wrapper. Rejected — it's an unnecessary layer that obscures the actual operation.

### D2: Rename SSBO block names (drop `I` suffix)

**Decision**: Rename shader buffer block names from the `*I` (int) convention to plain float names, and update the C++ binding strings accordingly.

| Old block name | New block name |
|---|---|
| `LinearPositionDeltaI` | `LinearPositionDelta` |
| `AngularPositionDeltaI` | `AngularPositionDelta` |
| `LinearVelocityDeltaI` | `LinearVelocityDelta` |
| `AngularVelocityDeltaI` | `AngularVelocityDelta` |

`PositionDeltaCount`, `VelocityDeltaCount`, and `ContactLagrange` are already correctly named and do not change.

**Rationale**: The `I` suffix was meaningful only to signal "this holds int bit patterns of floats." With native float buffers, it's misleading. Dropping it makes the code self-documenting.

**C++ impact**: ~10 string changes in `XpbdGpuSolver.cpp` (`srb.BindBuffer(...)` calls).

### D3: Declare extension per-file, not in common header

**Decision**: Add `#extension GL_EXT_shader_atomic_float : enable` to each of the 4 accumulate shaders directly, rather than in the `xpbd_atomic.glsl` common header.

**Rationale**: The extension is only needed by shaders that perform atomic float operations (the accumulate passes). Declaring it in `xpbd_atomic.glsl` would transitively enable it in all shaders that include the header, including apply shaders that don't need it and `clear_int_buffer.comp` which only writes zeros. Keeping extension declarations minimal reduces the SPIR-V feature surface.

**Alternative considered**: Declare in the common header for simplicity. Rejected — over-enabling extensions muddies the feature set and could complicate future portability to platforms with stricter extension validation.

### D4: Reuse `clear_int_buffer.comp` for float lagrange clearing

**Decision**: Keep `clear_int_buffer.comp` as-is, without changing its type declaration, and continue using it to clear the `ContactLagrange` float buffer.

**Rationale**: `clear_int_buffer.comp` writes `v[idx] = 0`. The IEEE-754 representation of `0.0f` is `0x00000000` — identical to `int(0)`. The shader works correctly for both types because it only zero-fills memory. Renaming the shader to `clear_float_buffer.comp` would be a cosmetic change with no functional benefit — and the shader is also used to clear genuinely integer buffers (e.g., `collision_count`).

### D5: Buffer allocation sizing

**Decision**: Change `sizeof(int)` to `sizeof(float)` in C++ buffer size calculations. The actual byte count is unchanged.

**Rationale**: Purely semantic — makes the code reflect what the buffers actually contain. On all supported platforms, both types are 4 bytes.

## Risks / Trade-offs

- **[GPU compatibility]** `GL_EXT_shader_atomic_float` requires `shaderAtomicFloat` (a Vulkan 1.1 optional feature). Risk: Integrated GPUs or older mobile GPUs may lack support. → Mitigation: This extension has been broadly supported since ~2015 on desktop (NVIDIA Maxwell+, AMD GCN2+, Intel Skylake+). If needed, a fallback path could be reintroduced behind a `#ifdef`, but this is out of scope for now.

- **[No behavior change]** The solve algorithm is unchanged. Risk: The CAS loop might have accidentally masked race conditions that native atomics expose differently. → Mitigation: Native atomics are strictly more correct than CAS loops — they guarantee forward progress. Any timing differences should only improve determinism.

- **[clear_int_buffer reuse]** Using an `int`-typed shader to clear float buffers relies on the bit representation of zero. → Mitigation: This is a well-known property of IEEE-754; all major GPU vendors guarantee it. The alternative (creating a separate float clear shader) would be cleaner but adds compilation overhead for no practical benefit.

## Open Questions

- Should we also rename `clear_int_buffer.comp` to something generic like `clear_buffer.comp`? (Deferred — purely cosmetic, low priority)
