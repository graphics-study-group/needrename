# Spike notes

Measured findings of the two spikes in task group 1. Each finding states what
was measured, what was observed, and what the change does about it. Findings
that changed a decision are cross-referenced from `design.md`.

Environment of the measurements:

- Windows 11, MSVC 19.5x (`Visual Studio 18 2026`), Debug and Release.
- Vulkan SDK 1.4.313.2 (`glslangValidator`, `spirv-opt`, `spirv-as`, `spirv-dis`).
- naga-cli 30.0.1 (cargo, host tool).
- wgpu-native v29.0.1.1 (`webgpu/webgpu.h`, `wgpu_native.dll`).
- Emscripten `latest` (emsdk, see the version recorded in section 1.4).

The corpus baked for the measurements is the engine's real one: the 12 render
shader assets under `builtin_assets/shaders` (compiled through `shader_baker`)
and the 42 physics kernels under `engine/Physics/shader` (compiled by the build
into `build/msvc/engine/Physics/spirv/debug`).

## 1.1 Converter fidelity on three representative shaders

The three shaders the task names, plus the rest of the corpus, were run through
the candidate converter (naga, SPIR-V to WGSL) and each output was validated by
the converter's own validator.

Results before any preprocessing (raw `glslangValidator -V` output into naga):

| Shader | Represents | Raw conversion |
| --- | --- | --- |
| `blinn_phong.frag` | texture sampling + constant block | fails: `unsupported instruction SpecConstantOp at Type` |
| `fluid.comp` | shared memory + uint atomics + storage image | fails: `Constant [34] '' is invalid: Initializer must be a const-expression` |
| `radix_scatter.comp` | shared memory + workgroup barrier | fails: `invalid id %14` (see F2) |

Wholesale result over the render corpus, after the specialization-constant
demotion of F1:

| Shader | Converted | WGSL validates |
| --- | --- | --- |
| `bloom.comp` | yes | yes |
| `fluid.comp` | yes | yes |
| `gaussian_blur.comp` | yes | yes |
| `debug_writethrough_mrt.frag` | yes | yes |
| `skybox.vert` | yes | yes |
| `debug_writethrough.vert` | yes | no (F4) |
| `shadowmap.vert` | yes | no (F4) |
| `blinn_phong.vert` | yes | no (F4) |
| `pbr_base.vert` | yes | no (F4) |
| `blinn_phong.frag` | no (F2) | — |
| `lambertian_cook_torrance.frag` | no (F2) | — |
| `skybox.frag` | no (F2) | — |

So the converter handles the *compute* half of the corpus (shared memory,
workgroup barriers, `uint` atomics, storage images, storage buffers) and fails
on three portability issues that the engine's own shaders expose, F1 to F4
below. None of the failures is a compute-semantics problem: they are all
interface-representation problems, which is what the bake is for.

### F1. Specialization constants have no WGSL equivalent and glslang emits `OpSpecConstantOp`

Three of the 12 render shaders declare them:

```
blinn_phong.frag:   layout(constant_id = 0) const int   SPECULAR_SHADING_MODE = 2;
bloom.comp:         layout(constant_id = 1) const float LUMINANCE_THRESHOLD  = 0.2;
fluid.comp:         layout(constant_id = 0..3) const float NU, KAPPA, K, dt
```

For a *comparison* against a specialization constant glslang emits an
`OpSpecConstantOp` whose operands are the constant and a literal:

```
%SPECULAR_SHADING_MODE = OpSpecConstant %int 2
%564 = OpSpecConstantOp %bool IEqual %SPECULAR_SHADING_MODE %int_1
```

naga's SPIR-V frontend rejects `OpSpecConstantOp` outright, and `spirv-opt
--freeze-spec-const` (alone or combined with `--ccp`,
`--eliminate-dead-code-aggressive`) does not fold it, so the failure survives
every tool-side attempt to remove it.

The engine does support specialization constants at pipeline creation
(`PipelineProperties::Shaders::specialization_constants`), but **no asset in the
repository sets any**: every `specialization_constants` array in
`builtin_assets`/`assets` is empty. The declared defaults are therefore the
values the engine has ever actually compiled.

**Resolution taken:** the WGSL variant is baked from the shader's own GLSL with
each `layout(constant_id = N) const` declaration demoted to a plain `const`, so
the values are frozen at bake time and no `OpSpecConstantOp` is generated. With
that rewrite `bloom.comp` and `fluid.comp` convert and validate. A material that
*does* request a specialization gets its own baked variant; the asset carries
the variants it was baked with, and a request for an unbaked value is a bake-time
error rather than a silent mismatch.

### F2. Combined image samplers do not convert at all

The smallest shader that reproduces it is 12 lines:

```glsl
#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 c;
layout(set=0,binding=0) uniform sampler2D tex;
void main(){ c = texture(tex, uv); }
```

naga 30.0.1 fails on it with `invalid id %14`. The instruction it cannot read is
the combined-image load glslang emits for every sampled texture:

```
%470 = OpTypeImage %float 2D 0 0 0 1 Unknown
%471 = OpTypeSampledImage %470
%474 = OpLoad %471 %base_tex          ; <-- rejected
%478 = OpImageSampleImplicitLod %v4float %474 %477
```

Replacing the load by a direct variable reference in the operand does not help
(naga then reports `invalid id %10`: it wants an expression, not a variable), so
the module cannot be repaired by a one-line edit; the combined-sampler form
itself has to go.

**Resolution taken:** the WGSL variant uses WebGPU's shape, which is *separate*
`texture2D` and `sampler` bindings. `design.md` D6 already required a
"combined-sampler split map" in the interface description; this measurement makes
that map mandatory rather than optional, and gives it a concrete encoding: the
original `(set, binding)` keeps the texture, and the sampler gets the next free
binding, both recorded in the description so the CPU-side binding code does not
invent its own numbering.

### F3. Descriptor arrays do not map to portable WebGPU

`engine/shader/include/engine/interface.glsl` declares

```glsl
layout(set = 0, binding = 1) uniform sampler2DArrayShadow light_shadowmaps[MAX_SHADOW_CASTING_LIGHTS];
```

naga turns that into

```wgsl
var light_shadowmaps: binding_array<texture_depth_2d_array, 1>;
```

`binding_array` is a naga extension, not core WGSL, and it corresponds to
`GPUBindGroupLayoutEntry::count`, i.e. the optional binding-array feature rather
than the portable baseline. The plan's non-goal list excludes bindless and
descriptor indexing, so the WGSL variant may not rely on it.

**Resolution taken:** arrays of opaque bindings are flattened to one binding per
element in the WGSL variant, and the description records the flattening. The
engine declares exactly one shadow-casting light (`MAX_SHADOW_CASTING_LIGHTS`
is 1), so the shipped shaders flatten to a single binding; the rule is stated in
the capability matrix as "opaque arrays are baked per element".

### F4. `std140` scalar arrays have no WGSL spelling, and naga does not synthesize one

`PerSceneUniform` ends with a `uint` array:

```glsl
uint light_cascade_count[MAX_SHADOW_CASTING_LIGHTS];
```

With `layout(std140)` Vulkan gives that member a 16-byte array stride. naga emits
`light_cascade_count: array<u32, 1>`, whose WGSL stride is 4, and the module then
fails validation:

```
error: Global variable [14] 'scene' is invalid
   = The array stride 4 is not a multiple of the required alignment 16
```

This is not a naga bug so much as a WGSL expressiveness limit: a `std140` array of
scalars needs an element wrapper with explicit padding (`@size(16)`), which naga
does not synthesize. Every vertex shader that reads `PerSceneUniform` therefore
fails validation (F1's table, last four rows) while still converting.

**Resolution taken:** the WGSL variant must carry a layout the WebGPU uniform
rules accept, and the interface description is what the CPU side writes against,
so the bake emits *padded* wrapper structs for scalar/`vec2` arrays inside
uniform blocks and records the resulting offsets in the description. The Vulkan
variant keeps reading the original SPIR-V and the engine's existing placement
code, so its byte layout is untouched. This is the one place where the two
backends legitimately disagree about a struct's internal offsets, which is
exactly why the offset table belongs in the baked description rather than being
derived at runtime.

### F5. Push constants have no WGSL equivalent

WGSL has no push-constant address space. naga does model
`naga::AddressSpace::PushConstant`, but its WGSL back end has no way to write
one, and WebGPU has no API for it either.

`skybox.vert` and 8 of the physics shaders declare a push-constant block;
`engine/shader/include/engine/interface.glsl` declares the render one
(`ModelTransform { mat4 model; int camera_id; int model_mat_index; }`).

**Resolution taken:** the bake rewrites the SPIR-V `PushConstant` variable into a
uniform-buffer variable at a reserved binding before conversion, and records that
binding in the description. This is the fallback `design.md` D6 anticipated; the
measurement confirms it is *required*, not optional. The reserved binding is the
last set's highest binding, chosen per shader so it cannot collide.

## 1.2 Constant-block conversion path

Same finding as F5, measured directly on a shader with a push-constant block
(`skybox.vert`, 48 bytes: `mat4`, `int`, `int`):

- Without the rewrite the converter produces no usable WGSL for the block.
- With the SPIR-V rewrite (storage class `PushConstant` → `Uniform`, plus
  `DescriptorSet`/`Binding` decorations at the reserved binding) the block
  converts to a WGSL `var<uniform>`, and the description records its size so the
  backend can bound its constant ring from the reflected size rather than a
  guessed constant.

The rewrite is therefore part of the bake, applied unconditionally: a shader
without a push-constant block passes through unchanged, and one with a block
gains the uniform form. The Vulkan artifact is not touched.

## 1.3 One compute scope per dispatch

Measured with `tools/webgpu_spike` (`ENGINE_BUILD_WEBGPU_SPIKE=ON`) on the
desktop validation build, wgpu-native v29.0.1.1, 2000 dispatches of a trivial
1-workgroup kernel, encode plus submit plus completion, whole run repeated:

| Shape | Wall time | Per-dispatch |
| --- | --- | --- |
| one compute scope, 2000 dispatches inside it | 4.1 – 5.1 ms | 2.1 – 2.6 µs |
| one compute scope per dispatch | 49.8 – 56.9 ms | 25 – 28 µs |
| difference | | **22 – 26 µs per extra scope** |

The scope boundary is therefore *not* free: it costs roughly ten times the
dispatch it wraps on this runtime. Two consequences the change has to live with:

- The per-dispatch-scope design is chosen for correctness (D3), not for speed,
  and its cost is bounded by the number of dispatches that need ordering. The
  physics step today records one barrier per ordering point, so the number of
  scopes tracks the number of barriers rather than the number of dispatches —
  which is why `design.md` can accept the cost.
- A backend that puts *every* dispatch in its own scope pays 22-26 µs per
  dispatch that is not inherently ordered. If the physics step turns out to be
  sensitive to that, the fix is to coalesce consecutive unordered dispatches
  into one scope, which needs no contract change. Task 9.3 re-measures this on
  the real step and is the point where the number gets compared against a full
  step's cost.

## 1.4 `webgpu.h` triangle plus one compute dispatch

_(pending: the Emscripten toolchain install finishes before this section can be
filled; the desktop half of the same program is in sections 1.3 and 1.5)_

The desktop half already proves the parts that are not Emscripten-specific: one
triangle rasterised and read back (the centre pixel is exactly the fragment
shader's colour, 1152 of 4096 pixels covered), and one dispatch whose 256
elements read back as `index * 2 + 1`.

Two implementation findings came out of making those pass, and both shape the
backend:

### F6. Zero-valued defaults silently disable work

Value-initialising a descriptor is *not* the same as using its `_INIT` macro.
`WGPUMultisampleState::mask` is a coverage mask, so a zero-valued descriptor
masks every sample out and the rasteriser produces nothing — with no validation
error and no device message, because the pipeline is legal. The first version of
the harness rendered a correct clear and zero triangle pixels for exactly this
reason.

**Resolution taken:** the WebGPU backend initialises every descriptor through the
`WGPU_..._INIT` macros (or assigns all of its fields), and a compile-time check
in the backend keeps a plain `= {}` from creeping back in for the structs whose
zero value is meaningful. Every field that has a non-zero default is a place
where the same silent failure can hide.

### F7. A queue write is a copy, not a mapped write

`wgpuQueueWriteBuffer` is implemented as a copy, so the destination buffer needs
`COPY_DST` in addition to whatever the shader sees; without it the call fails
validation (`Usage flags BufferUsages(COPY_SRC | STORAGE) ... do not contain
required usage flags BufferUsages(COPY_DST)`).

**Resolution taken:** the mapped-window contract's flush path (D5) adds
`COPY_DST` to every host-writable buffer, and the backend's upload path uses the
same rule. This is also why the ring buffer for kernel parameters is created
`UNIFORM | COPY_DST`.

## 1.5 Limits and two dispatches in one scope

Limits reported by the desktop validation runtime (wgpu-native v29.0.1.1):

| Limit | Value |
| --- | --- |
| `maxBindGroups` | 8 |
| `maxBufferSize` | 1 TiB |
| `maxStorageBufferBindingSize` | 2 147 483 644 |
| `maxUniformBufferBindingSize` | 65 536 |
| `maxComputeWorkgroupsPerDimension` | 65 535 |
| `maxComputeInvocationsPerWorkgroup` | 1 024 |
| `maxStorageBufferBindingSize`... | see above |
| `minUniformBufferOffsetAlignment` | **64** |
| `minStorageBufferOffsetAlignment` | 32 |

Two consequences:

- `maxBindGroups` is 8 here but the WebGPU *default* is 4, and the render path
  uses scene (0), camera (1) and material (2) plus the kernel-parameter window.
  That fits in 4, so no limit raise is required; the backend still requests the
  default explicitly and fails loudly rather than silently shrinking.
- `minUniformBufferOffsetAlignment` is 64 on this runtime and 256 in the
  browser's Dawn. The kernel-parameter ring therefore aligns every slice to 256
  bytes: the browser's number is the binding one, and using it everywhere keeps
  one code path.
- `maxUniformBufferBindingSize` is 64 KiB, which bounds how large a reflected
  push-constant block may be before the ring cannot hold it. The engine's
  largest push-constant block is the 72-byte render one, so there is no
  pressure, but the bake records the size and the backend checks it.

Two dispatches sharing one compute scope, where the second reads the word the
first writes: **64 of 64 runs on the desktop runtime observed the earlier
dispatch's write**. That is an observation, not a guarantee — WGSL gives no
ordering inside a scope, and the specification explicitly leaves it undefined.
The per-dispatch-scope design does not depend on it either way; the number is
recorded so that a future "coalesce unordered dispatches into one scope"
optimisation knows what it would be relying on.
